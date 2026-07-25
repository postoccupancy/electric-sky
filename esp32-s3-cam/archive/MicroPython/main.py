import network, time, ujson
import urequests
from machine import I2C, Pin

import bme280
from secrets import WIFI_SSID, WIFI_PASS, SERVER_ENDPOINT, TOKEN, DEVICE_ID


# ---- Wi-Fi connect ----
def wifi_connect():
    sta = network.WLAN(network.STA_IF)
    sta.active(True)
    if not sta.isconnected():
        sta.connect(WIFI_SSID, WIFI_PASS)
        t0 = time.ticks_ms()
        while not sta.isconnected():
            if time.ticks_diff(time.ticks_ms(), t0) > 15000:
                raise RuntimeError("WiFi timeout")
            time.sleep_ms(200)
    return sta.ifconfig()


# ---- Optional: sync RTC via NTP (if available) ----
def ntp_sync():
    try:
        import ntptime
        ntptime.settime()  # sets RTC to UTC
        print("NTP OK:", time.localtime())
    except Exception as e:
        print("NTP failed:", e)


# ---- BME280 read ----
# Lower frequency (10000) is more stable with loose jumper connections;
# can raise to 100000 after soldering.
i2c = I2C(0, scl=Pin(41), sda=Pin(42), freq=10000)
bme = bme280.BME280(i2c=i2c)

def bme280_read():
    vals = bme.values  # ('32.03C', '997.77hPa', '24.94%')
    temp_c = round(float(vals[0].replace('C', '')), 1)
    temp_f = round(temp_c * 9 / 5 + 32, 1)
    rh     = round(float(vals[2].replace('%', '')), 1)
    pres   = round(float(vals[1].replace('hPa', '')), 1)
    return temp_c, temp_f, rh, pres


# ---- Boot: WiFi + (optional) NTP ----
print("WiFi:", wifi_connect())
ntp_sync()

# LED on GPIO4 as simple digital output
led = Pin(4, Pin.OUT)
led.value(0)  # start off

SENSOR_INTERVAL_S = 2  # seconds

while True:
    try:
        # 1) Read sensor
        temp_c, temp_f, rh, pres = bme280_read()

        # 2) Build payload (ts set server-side)
        payload = {
            "device_id": DEVICE_ID,
            "temp_c": temp_c,
            "temp_f": temp_f,
            "rh":     rh,
            "pres":   pres,
        }
        
        import gc

        # 3) POST to FastAPI
        try:
            gc.collect()  # free memory before HTTPS request
            r = urequests.post(
                SERVER_ENDPOINT,
                headers={
                    "Content-Type": "application/json",
                    "X-Ingest-Token": TOKEN,
                },
                data=ujson.dumps(payload),
            )
            if r is None:
                print("POST failed: no response (SSL/memory?)")
            else:
                status = r.status_code
                print("POST status:", status, r.text)
                r.close()

                if 200 <= status < 300:
                    led.value(1)
                    time.sleep_ms(100)
                    led.value(0)

        except Exception as e:
            print("POST failed:", repr(e))

    except OSError as e:
        print("sensor/WiFi err:", e)
        # brief backoff before trying again
        time.sleep_ms(200)

    # Wait before next reading
    time.sleep(SENSOR_INTERVAL_S)