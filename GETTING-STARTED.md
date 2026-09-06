# Getting started

This walks you from nothing to a working control panel on your phone. It takes
about ten minutes the first time.

You need an ESP32 or ESP8266, a USB cable, and a WiFi network your phone is
also on. No extra parts.

## 1. Install the Arduino IDE

Get it from [arduino.cc/en/software](https://www.arduino.cc/en/software) if you
do not have it. Version 2.x is fine.

## 2. Add support for your board

The IDE does not know about ESP32 out of the box.

Open **File > Preferences**. Near the bottom there is a box called
**Additional boards manager URLs**. Paste this in:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

If you have an ESP8266 instead, use this one:

```
https://arduino.esp8266.com/stable/package_esp8266com_index.json
```

Click OK. Now open **Tools > Board > Boards Manager**, search for `esp32` (or
`esp8266`), and install it. It is a big download, so leave it running.

## 3. Install three libraries

Open **Tools > Manage Libraries**. Search for and install:

- **WebSockets** by Markus Sattler
- **ArduinoJson** by Benoit Blanchon, version 7 or newer

Bench is not in that list yet, so it installs differently. Download
[Bench.zip from the latest release](../../releases/latest), then in the IDE go
to **Sketch > Include Library > Add .ZIP Library** and pick the file you
downloaded.

## 4. Open the example

**File > Examples**, scroll down to **Bench**, and open **Minimal**.

If Bench is not in that list, the ZIP did not install. Restart the IDE and
look again.

## 5. Put your WiFi details in

Near the top of the sketch there are two lines:

```cpp
const char *WIFI_SSID = "YOUR_WIFI";
const char *WIFI_PASS = "YOUR_PASSWORD";
```

Replace those with your actual network name and password. Keep the quotes.

Your network needs to be 2.4GHz. ESP32 and ESP8266 cannot see 5GHz networks at
all, and this is the single most common reason a board never connects. If your
router shows one network name for both, look in its settings for a 2.4GHz-only
name, or temporarily split them.

## 6. Flash it

Plug the board in. Then:

- **Tools > Board** and pick your board. If you are not sure, "ESP32 Dev
  Module" works for most ESP32 boards.
- **Tools > Port** and pick the one that appeared when you plugged in. On
  Windows it is a COM number, on Mac it starts with `/dev/cu.`.
- Click the arrow button to upload.

If it sits at `Connecting........` and fails, hold the **BOOT** button on the
board while it uploads, then let go. Some boards need that.

## 7. Check it worked

Open **Tools > Serial Monitor** and set the speed to **115200** in the dropdown
on the right. Press the reset button on the board.

You should see something like:

```
....
IP: 192.168.1.42
[Bench] listening on 81
[Bench] discoverable as bench.local
```

That IP is your board. Write it down, though you probably will not need it.

If you only see garbage characters, the speed is wrong. Set it to 115200.

If it prints dots forever, it cannot join your WiFi. Check the password, and
check the 2.4GHz thing from step 5.

## 8. Open the app

Install Bench on your phone, then tap **Find my board**.

It should appear within a few seconds. Tap it.

If nothing shows up, tap **Enter address manually** and type the IP from the
serial monitor, with port `81` and path `/ws`. There is a **Test connection**
button that tells you straight away whether it can reach the board.

## 9. Build your panel

The board tells the app what it has. Tap **Build it for me** and you get a
working panel immediately.

Flip the relay toggle and the onboard LED should change. That is the whole
loop working: phone to board and back.

## Making it yours

Open the sketch again. Every variable you want on your phone gets one line in
`setup()`:

```cpp
bench.number("temp", "Temperature", &temperature).unit("C").range(0, 50).readOnly();
bench.boolean("relay", "Relay", &relay);
```

The first argument is a short key the app stores. Do not change it later, or
saved panels stop finding it. The second is the label you see on screen.

`.readOnly()` means the app can show it but never change it. Use it for
sensors. Leave it off for anything you want to control from your phone.

Then read your sensors into those variables in `loop()`, and Bench sends them
across on its own.

The **Setup** tab in the app will write this code for you if you would rather
describe your board there and paste the result.

## When it goes wrong

**The app finds nothing.** Phone and board must be on the same network. A guest
network or a separate IoT network will not work. Some mesh routers block the
discovery traffic, in which case type the IP in by hand.

**It connects then drops.** Usually power. A board browning out under WiFi load
does this. Try a different USB cable or a proper supply rather than a laptop
port.

**Values are stuck.** Check you are not calling `delay()` in `loop()`. It
blocks everything, including the connection. Use `millis()` to time things
instead, the way the example does.

**A control does nothing.** That channel is probably marked `.readOnly()`.

**It stops after you reflash.** Normal. The app reconnects on its own within a
few seconds.

## Next

Look at the **Greenhouse** example for a bigger setup with four sensors, a PWM
fan and a momentary pump button. ESP32 only, since it uses `ledcAttach`.

The [README](README.md) has the full API and the wire protocol if you want to
implement it yourself.
