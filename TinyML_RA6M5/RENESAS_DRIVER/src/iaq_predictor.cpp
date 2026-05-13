/**
 * @file  iaq_predictor.cpp
 * @brief TFLite Micro inference engine for IAQ future-state forecasting.
 *
 * Architecture: MLP 1 -> 32 -> 16 -> 1
 *   Input:  1 value  — z-score normalized TVOC (ppb)
 *   Output: 1 value  — predicted future IAQ rating (1.0 - 5.0, UBA scale)
 *
 * Key design choices:
 *   - Static allocation only (no heap / new / malloc).
 *   - MicroMutableOpResolver with only required ops to minimize Flash usage.
 *     Do NOT use AllOpsResolver (removed in newer TFLite) or the full
 *     MicroMutableOpResolver header (it pulls in signal/irfft.h -> KissFFT).
 *   - tensor_arena must be alignas(16); otherwise Cortex-M33 will HardFault.
 *   - kTensorArenaSize of 4KB covers this model. If AllocateTensors() fails,
 *     increase by 2KB increments and rebuild.
 *
 * Future-forecast semantics:
 *   The model predicts what IAQ *will be* after ambient conditions stabilize,
 *   not the instantaneous reading. This enables early warning before IAQ
 *   deteriorates to an unhealthy level.
 */

#include "iaq_predictor.h"
#include "iaq_model_data.h"    /* g_iaq_model_data[] — fallback model in Code Flash */
#include "scaler_constants.h"  /* IAQ_MEAN[], IAQ_SCALE[] — z-score params    */
#include "drv_flash_hp.h"      /* DATA_FLASH_BASE, DATA_FLASH_LAST_BLOCK_ADDR */

/* TFLite Micro headers — include only what is needed */
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_log.h"

/* -------------------------------------------------------------------------
 * Memory configuration
 * ------------------------------------------------------------------------- */
constexpr int kTensorArenaSize = 4 * 1024;
alignas(16) static uint8_t tensor_arena[kTensorArenaSize];

/* -------------------------------------------------------------------------
 * Module-private state
 * ------------------------------------------------------------------------- */
static const tflite::Model*    s_model      = nullptr;
static tflite::MicroInterpreter* s_interpreter = nullptr;
static TfLiteTensor*           s_input      = nullptr;
static TfLiteTensor*           s_output     = nullptr;

/* -------------------------------------------------------------------------
 * Op resolver — only ops used by this MLP model
 * FullyConnected : Dense layers
 * Relu           : Hidden-layer activation
 * (No signal ops, no KissFFT dependency)
 * ------------------------------------------------------------------------- */
static tflite::MicroMutableOpResolver<2> s_resolver;

/* -------------------------------------------------------------------------
 * NVS block magic written by fwupdate_receiver after a successful OTA
 * transfer. Layout at DATA_FLASH_LAST_BLOCK_ADDR (last 64-byte block):
 *   [0..3]  0xDE 0xAD 0xBE 0xEF  — valid-model marker
 *   [4..7]  model_len big-endian uint32_t
 *   [8..9]  model_crc big-endian uint16_t
 * ------------------------------------------------------------------------- */
#define IAQ_NVS_MAGIC_0  0xDEU
#define IAQ_NVS_MAGIC_1  0xADU
#define IAQ_NVS_MAGIC_2  0xBEU
#define IAQ_NVS_MAGIC_3  0xEFU

/**
 * Check whether a valid OTA model was written to Data Flash.
 * Returns true and sets *out_len if the NVS block contains a valid marker.
 * Called once from IAQ_Init().
 */
static bool iaq_flash_model_valid(uint32_t *out_len)
{
    const volatile uint8_t *nvs =
        (const volatile uint8_t *)(uintptr_t)DATA_FLASH_LAST_BLOCK_ADDR;

    if (nvs[0] != IAQ_NVS_MAGIC_0 || nvs[1] != IAQ_NVS_MAGIC_1 ||
        nvs[2] != IAQ_NVS_MAGIC_2 || nvs[3] != IAQ_NVS_MAGIC_3)
    {
        return false;
    }

    uint32_t len = ((uint32_t)nvs[4] << 24U) | ((uint32_t)nvs[5] << 16U) |
                   ((uint32_t)nvs[6] <<  8U) |  (uint32_t)nvs[7];

    /* Sanity: length must be non-zero and fit within the model region */
    if (len == 0UL || len > (DATA_FLASH_SIZE - DATA_FLASH_BLOCK_SIZE))
    {
        return false;
    }

    *out_len = len;
    return true;
}

/* =========================================================================
 * IAQ_Init
 * ========================================================================= */
bool IAQ_Init(void)
{
    /* Register only the ops this model uses */
    s_resolver.AddFullyConnected();
    s_resolver.AddRelu();

    /* ------------------------------------------------------------------
     * Model source selection:
     *   1. If fwupdate_receiver wrote a valid OTA model to Data Flash
     *      (NVS magic present at DATA_FLASH_LAST_BLOCK_ADDR), use the
     *      memory-mapped Data Flash pointer so the new model is active
     *      immediately after reset without reflashing Code Flash.
     *   2. Otherwise fall back to the compiled-in g_iaq_model_data[].
     * ------------------------------------------------------------------ */
    const uint8_t *model_ptr  = g_iaq_model_data;
    const char    *model_src  = "Code Flash (built-in)";
    uint32_t       flash_len  = 0UL;

    if (iaq_flash_model_valid(&flash_len))
    {
        model_ptr = (const uint8_t *)(uintptr_t)DATA_FLASH_BASE;
        model_src = "Data Flash (OTA)";
        MicroPrintf("IAQ: using OTA model from Data Flash (%lu bytes)", flash_len);
    }
    else
    {
        MicroPrintf("IAQ: using built-in model (%u bytes)", g_iaq_model_data_len);
    }
    (void)model_src;   /* suppress unused-variable warning if MicroPrintf omitted */

    /* Map model from selected source (no copy — zero allocation) */
    s_model = tflite::GetModel(model_ptr);
    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        MicroPrintf("IAQ model schema mismatch: expected %d, got %d",
                    TFLITE_SCHEMA_VERSION, (int)s_model->version());
        return false;
    }

    /* Build interpreter — static to avoid heap allocation */
    static tflite::MicroInterpreter static_interpreter(
        s_model, s_resolver, tensor_arena, kTensorArenaSize);
    s_interpreter = &static_interpreter;

    /* Allocate memory for all tensors (input, hidden activations, output) */
    TfLiteStatus status = s_interpreter->AllocateTensors();
    if (status != kTfLiteOk) {
        MicroPrintf("IAQ AllocateTensors() failed — increase kTensorArenaSize");
        return false;
    }

    /* Cache tensor pointers for fast access during inference */
    s_input  = s_interpreter->input(0);
    s_output = s_interpreter->output(0);

    return true;
}

/* =========================================================================
 * IAQ_Predict
 *
 * Pipeline:
 *   1. Z-score normalize: x_scaled = (tvoc_ppb - MEAN) / SCALE
 *   2. Write scaled value into input tensor
 *   3. Run forward pass (Invoke)
 *   4. Read output tensor — clamp to valid UBA range [1.0, 5.0]
 *
 * The output represents the FORECASTED steady-state IAQ, not the
 * instantaneous reading. Call this function once per sensor cycle to
 * build a rolling 1-step-ahead prediction.
 * ========================================================================= */
float IAQ_Predict(float tvoc_ppb)
{
    if (s_interpreter == nullptr) {
        return -1.0f; /* IAQ_Init() was not called or failed */
    }

    /* Step 1: Z-score normalization using training-set statistics */
    float tvoc_scaled = (tvoc_ppb - IAQ_MEAN[0]) / IAQ_SCALE[0];

    /* Step 2: Load normalized input into tensor */
    s_input->data.f[0] = tvoc_scaled;

    /* Step 3: Run inference */
    TfLiteStatus status = s_interpreter->Invoke();
    if (status != kTfLiteOk) {
        MicroPrintf("IAQ Invoke() failed");
        return -1.0f;
    }

    /* Step 4: Read and clamp output to valid UBA range */
    float iaq_forecast = s_output->data.f[0];
    if (iaq_forecast < 1.0f) iaq_forecast = 1.0f;
    if (iaq_forecast > 5.0f) iaq_forecast = 5.0f;

    return iaq_forecast;
}
