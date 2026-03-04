import requests
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from sklearn.preprocessing import StandardScaler
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Dense, Dropout
from tensorflow.keras.optimizers import Adam

# Download AQI data from Meteo_API
url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=10.8231&longitude=106.6297&hourly=pm2_5,pm10,carbon_monoxide,nitrogen_dioxide,ozone,us_aqi&start_date=2024-01-01&end_date=2024-12-31"
response = requests.get(url)
data = response.json()
df = pd.DataFrame(data["hourly"])

# Format the window_size
features = ["pm2_5", "pm10", "carbon_monoxide", "nitrogen_dioxide", "ozone"]
target = "us_aqi"

WINDOW_SIZE = 6

def create_windows(df, window_size):
    x,y = [], []
    for i in range(len(df)-window_size):
        x.append(df[features].iloc[i: i + window_size].values.flatten())
        y.append(df[target].iloc[i + window_size])
    return np.array(x), np.array(y)

x,y = create_windows(df, WINDOW_SIZE)

# Split train/test (80/20) - without shuffle to keep the contination of time
split = int(0.8*len(x))
x_train, x_test = x[:split], x[split:]
y_train, y_test = y[:split], y[split:]

# Format the data
scaler = StandardScaler()
x_train_scaled = scaler.fit_transform(x_train)
x_test_scaled = scaler.transform(x_test)

print(f"Input shape: {x_train_scaled.shape}") # (N,30) because 6 hours * 5 features

# --- Build up Model MLP (for RA6M5)
from tensorflow.keras.layers import Dense, Input

input_shape = WINDOW_SIZE * len(features)

model = Sequential([
    Input(shape=(input_shape,)),
    Dense(16, activation='relu'),
    Dense(8, activation='relu'),
    Dense(1) #Predict AQI values continously
])

model.compile(optimizer='adam', loss='mse', metrics=['mae'])

# Training model (NOTE: SHOULD TRAIN ON KAGGLE NOTEBOOK)
from tensorflow.keras.callbacks import EarlyStopping

# Early stop for avoid wasting GPU
early_stop = EarlyStopping(monitor='val_loss', patience=10, restore_best_weights=True) 

history = model.fit(
    x_train_scaled, y_train,
    epochs=100,
    batch_size=32,
    validation_split=0.1,
    callbacks=[early_stop],
    verbose=1 # Nên để 1 để theo dõi tiến độ lúc đầu
)
# ---Review & Generate output
mae = model.evalute(x_test_scaled, y_test, verbose=0)[1]
print(f"Model MAE: {mae:.2f}")

# IMPORTANT: SAVE THIS NUMBERS FOR APPLY WITH C LANGUAGE ON KIT
print(f"Mean (6 hours (marks) x 5 features): \n{scaler.mean_}")
print(f"Scale (6 hours (marks) x 5 features): \n{scaler.scale_}")

