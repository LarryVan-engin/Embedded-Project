#ifndef SERVER_COMM_H
#define SERVER_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

void server_comm_init(void);
void server_comm_publish_raw(float tvoc_ppb, float eco2_ppm);
void server_comm_publish_predict(float iaq_predict);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_COMM_H */
