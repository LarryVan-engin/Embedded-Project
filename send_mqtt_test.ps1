# MQTT Test Commands for Windows PowerShell

Write-Host "===== MQTT Test Messages =====" -ForegroundColor Cyan
Write-Host ""

# Test 1
Write-Host "Test 1: Send TVOC + IAQ data" -ForegroundColor Green
$msg1 = '[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80'
mosquitto_pub -h localhost -t "iaq/node/data" -m $msg1
Write-Host "Sent: $msg1" -ForegroundColor Yellow
Start-Sleep -Milliseconds 500

# Test 2
Write-Host ""
Write-Host "Test 2: Send Temperature + Humidity" -ForegroundColor Green
$msg2 = '[548171 ms] [sensor:263] T=31.1 C  RH=46.9%'
mosquitto_pub -h localhost -t "iaq/node/data" -m $msg2
Write-Host "Sent: $msg2" -ForegroundColor Yellow
Start-Sleep -Milliseconds 500

# Test 3
Write-Host ""
Write-Host "Test 3: Good air quality" -ForegroundColor Green
$msg3 = '[550000 ms] Published: TVOC=50.0ppb | Actual=1.2 | Predict=1.25'
mosquitto_pub -h localhost -t "iaq/node/data" -m $msg3
Write-Host "Sent: $msg3" -ForegroundColor Yellow
Start-Sleep -Milliseconds 500

# Test 4
Write-Host ""
Write-Host "Test 4: Medium air quality" -ForegroundColor Green
$msg4 = '[551000 ms] Published: TVOC=200.0ppb | Actual=3.0 | Predict=2.95'
mosquitto_pub -h localhost -t "iaq/node/data" -m $msg4
Write-Host "Sent: $msg4" -ForegroundColor Yellow
Start-Sleep -Milliseconds 500

# Test 5
Write-Host ""
Write-Host "Test 5: Poor air quality" -ForegroundColor Green
$msg5 = '[552000 ms] Published: TVOC=500.0ppb | Actual=4.5 | Predict=4.4'
mosquitto_pub -h localhost -t "iaq/node/data" -m $msg5
Write-Host "Sent: $msg5" -ForegroundColor Yellow
Start-Sleep -Milliseconds 500

# Test 6
Write-Host ""
Write-Host "Test 6: Low temperature" -ForegroundColor Green
$msg6 = '[553000 ms] [sensor:264] T=5.0 C  RH=30.0%'
mosquitto_pub -h localhost -t "iaq/node/data" -m $msg6
Write-Host "Sent: $msg6" -ForegroundColor Yellow
Start-Sleep -Milliseconds 500

# Test 7
Write-Host ""
Write-Host "Test 7: High humidity" -ForegroundColor Green
$msg7 = '[554000 ms] [sensor:265] T=28.0 C  RH=85.5%'
mosquitto_pub -h localhost -t "iaq/node/data" -m $msg7
Write-Host "Sent: $msg7" -ForegroundColor Yellow
Start-Sleep -Milliseconds 500

# Test 8
Write-Host ""
Write-Host "Test 8: Very high TVOC" -ForegroundColor Green
$msg8 = '[555000 ms] Published: TVOC=1000.0ppb | Actual=5.0 | Predict=4.9'
mosquitto_pub -h localhost -t "iaq/node/data" -m $msg8
Write-Host "Sent: $msg8" -ForegroundColor Yellow

Write-Host ""
Write-Host "===== All tests sent! =====" -ForegroundColor Cyan
Write-Host "Check Terminal 2 (Backend) for logs" -ForegroundColor Cyan
