Nanopb install instructions

This project uses protobuf framing for RadioSignal. For robust protobuf encoding/decoding on Arduino/ESP32 please install nanopb and generate C sources from `.proto`.

Arduino IDE / Arduino CLI:
- Use the Library Manager and install `nanopb` (search "nanopb").
- Generate `.pb.c`/`.pb.h` files from `.proto` using the nanopb generator (Python script `nanopb_generator.py` or the online generator), and add them to the firmware project (e.g., put under `main/` or `main/proto/`).

PlatformIO:
- Add `lib_deps = nanopb` to `platformio.ini` or install via `pio lib install "nanopb"`.
- Generate `.pb.c` and `.pb.h` and include them in `src/` or add them as a library.

Notes:
- After adding nanopb-generated files, replace `encode_radio_signal_pb` with `pb_encode()` calls and use the same framing `[0xAA 0x55][u16 length][payload]` used elsewhere in this project.
- Ensure BLE/Serial transports pass raw framed bytes (not JSON) so the Rust listener can decode them.
