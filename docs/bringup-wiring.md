# Bring-up wiring and tests

Hardware: the **Waveshare ESP32‑S3‑Touch‑LCD‑3.5** panel, with test parts on jumper wires to its J8 header until the I/O backpack exists.

## Waveshare panel

Pins: [src/board/pins.h](../src/board/pins.h). Flash over the panel's USB‑C with `pio run -t upload`; the console is on the same port (USB CDC). If the upload doesn't start, hold BOOT and press RESET once.

**Without anything on J8**, the panel alone should show the UI, react to touch and play the start chime. Then:
- In the console, run `i2c`. Expect 0x18 ES8311, 0x20 TCA9554, 0x34 AXP2101, 0x38 FT6336, 0x51 PCF85063 and 0x6A/0x6B QMI8658. Note anything missing or extra.
- The console warns that the front panel expander 0x22 is missing. That is expected without a backpack; the buttons are just disabled.

**Test I/O on J8.** Take the J8 pin numbers from the Waveshare wiki pinout image and note them here once confirmed.

| Item | GPIO | How to wire for the test |
|---|---|---|
| OUT1–OUT4 | 38 / 39 / 40 / 41 | LED + 330 Ω to GND each |
| PWR1 / PWR2 | 42 / 21 | LED + 330 Ω to GND (brightness follows the PWM slider) |
| DS18B20 data | 17 | 4.7 kΩ pull-up to J8 **3V3**; sensor VDD to 3V3, GND to GND |
| Level switch | 18 | switch to GND |
| Front buttons and LEDs | I2C 7 / 8 | a TCA9535 or PCA9535 breakout set to address **0x22** (A1 high, A0/A2 low), powered from J8 3V3. P00 Start and P01 Pump: push button to GND with a 10 kΩ pull-up to 3V3 each. P02 / P03: LED + 1 kΩ to GND |
| Speaker | panel connector | 8 Ω speaker on the panel's MX1.25 speaker socket |

Never connect a real SSR, pump or heater during bring-up. Leave the camera unplugged (its pins are the outputs).

**Checks for this panel** (note the results in `docs/design.md`):
1. **Orientation:** the picture is landscape and the right way up. If it is upside down, build with `-DMT_LCD_ROTATION=3`. Colours are wrong? Try `-DMT_LCD_INVERT=0` or `-DMT_LCD_BGR=1`.
2. **Touch** hits the right spot in all four corners (see step 2 below).
3. **Sound:** the start chime plays. The amplifier enable (PA_CTRL) is TCA9554 EXIO7, switched on after the codec init. If the codec answers (`i2c` shows 0x18 and 0x20) but there is no sound, look for "amplifier enable … failed" in the boot log and check the speaker connector.
4. **Power from J8:** feed 5 V into J8 VBUS (no USB) and check that the panel starts without pressing PWR.
5. **Wi‑Fi range** with the panel in its frame in the box.

## What to check

1. After flashing, the screen shows the **MashTouch** home screen in landscape, with readable text. Open **Tools > Output and button test** for the next steps.
   - Colours inverted (black background looks white)? Build with `-DMT_LCD_INVERT=0`.
   - Red and blue swapped? Build with `-DMT_LCD_BGR=1`.
2. Touch a corner: **Touch x,y** at the top of the test screen should show about 0,0 at the top left and 479,319 at the bottom right. If the axes are swapped or mirrored, note what you see (and the Home button at the top left will be hard to hit).
3. Tap **OUT1–OUT4**: the matching LED switches on and off. (Bench-test values only work while the controller is idle.)
4. Move the **PWR** sliders: LED brightness follows.
5. **Sound** plays the "brew finished" melody through the speaker; a short chime also plays at power-on. The backlight slider dims the screen.
6. Hold a front button: the status line shows *held*. (Presses now go to the controller: see below.)
7. With a DS18B20 connected, its temperature appears in large digits. Tap **Scan** after plugging one in.
8. The status line shows free heap and PSRAM. PSRAM should be close to 8000 k; if it shows 0, PSRAM is not enabled in the build.

## Testing the brewing core (serial console)

Open the serial monitor at 115200 baud (task **PIO: Serial Monitor**) and type `help`. The bottom line of the bring-up screen shows the controller state.

Fake heating works without a sensor or heater. In this mode the physical heater and pump outputs stay off.

```
sim on 20       fake kettle (20 L, 2.5 kW), clock 20x faster
log 2           print a status line every 2 s
auto            start the default recipe: answer prompts with "yes"
skip            jump to the next step
sim temp 24     jump the fake kettle temperature (e.g. to finish cooling)
stop
manual          manual mode: "heat" toggles heating, "sp 66" sets the setpoint
tune 65         PID autotune at 65 C; prints the new gains when done
```

Front buttons:

- **Start**, short press: answer *yes* to a prompt or confirmation. Otherwise it pauses or resumes (in manual mode it switches heating on or off).
- **Start**, long press: ask to skip the step. A second long press changes the question to stop. Confirm with a short press. The question times out after 10 s.
- **Pump**, short press: toggle the pump. In automatic mode this overrides the pump until the next step.
- **Pump**, hold: run the pump while held (prime).

Then repeat with LEDs on OUT1 (heater) and PWR1 (pump), and a real DS18B20 in a cup of hot water: `sim off`, `manual`, `heat`, `sp 40`.

## Testing storage

1. After the first boot, the console shows `LittleFS .../... kB, recipe 'Single infusion'`. `fs` shows the usage.
2. Run `set heater.kp 25` and reset the board. `get heater` still shows 25.
3. Run `recipe new Test`, then `rset steps.0.minutes 5`, then `recipes`. After a reset, `test` is still marked active.
4. **Power-loss resume** (needs a real or simulated sensor reading, not `sim`, because no resume point is written in fake-heating mode):
   - With a DS18B20 attached, start `auto`, answer `yes` and let it run for a minute.
   - Pull the USB cable and plug it back in.
   - The console offers the brew. `resume` continues it in the same phase.
5. Run `set heater.boilPowerPct 150`: it must be refused.

## Testing the touch UI

The bring-up screen is now **Tools > Output and button test**. Leaving it switches all test outputs off.

1. **Home** shows the temperature and the recipe. Tap **Tools** and switch on **Fake heating**. In Settings > Simulator, set **Clock speed** to 20.
2. **Home > Brew > Start now.** The brew screen opens with the *Is the water in the kettle?* panel. Tap **Yes**.
3. Watch the main card: the temperature rises, turns green once the step temperature is reached, and the timer starts. Tap the timer and set it to 1 minute to move on quickly.
4. A short tap on **Skip** only shows *Hold to skip*. Holding it skips the step.
5. During the boil, each hop addition opens an **Add now** message.
6. A long press on the front **Start** button shows the red skip/stop panel. A second long press switches it to stop, and a short press confirms.
7. Change a step in **Recipes > Edit active** and check it with the `recipe` console command. Change a value in **Settings** and check it with `get`.
8. With **Fahrenheit** on (Settings > Display), every temperature shows and edits in °F.
9. Check that every touch target is easy to hit through the front panel, and that the long press of 0.8 s for Skip/Stop feels right.

## Testing recipe import

1. `import selftest`: every line must say PASS.
2. `import`, then paste a BeerXML export from your brewing app. The console lists what it imported and what it skipped (dry hops, secondary additions...).
3. `recipes` shows the new recipe. Run `recipe use <id>` and check the steps and additions in `recipe`, or on the screen in **Recipes > Edit active**.
4. Import the same file again: it is added as `<id>-2`, and nothing is overwritten.

## Testing the network

1. **First boot, no home network:** the console prints `Wi-Fi: access point MashTouch-XXXX`. Tools > Wi‑Fi on the screen shows its name, password and a QR code.
2. Join it with a phone (scan the QR code). The phone should offer to open the MashTouch page; if not, open http://192.168.4.1.
3. **Joining your home network:** System tab > Scan networks > Connect, enter the password (or use the touch screen). The console prints the IP. After about 30 s the access point closes.
4. From a PC on the home network, open http://mashtouch.local (or the IP).
   - Brew tab: start fake heating (console `sim on 20`), then **Start brew**. Answer the prompts in the browser and on the touch screen; both stay in step.
   - Recipes tab: import your BeerXML file, open it, check the timeline.
   - Settings tab: change a value and check it with `get` on the console.
5. **Firmware update:** System tab > choose `.pio/build/ws35/firmware.bin` > Upload. The controller restarts with the new build. Try the same with `pio run -e ws35_ota -t upload`.
6. **Web password:** `set net.webPassword "secret"`. The browser now asks for a login (user `admin`) when you change something.
7. Switch the home router off: after 20 s the setup access point opens again; with the router back, the controller reconnects.
8. Check the Wi‑Fi signal (RSSI on the Wi‑Fi screen) with the controller inside the closed box.
