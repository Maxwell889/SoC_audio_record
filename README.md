# SoC Audio Record

Course SoC audio recording project for DE10-Lite / MAX 10.

This public snapshot contains the project-owned audio recording logic and software:

- INMP441 I2S receiver RTL
- Synchronous FIFO and AHB audio register interface
- IMA ADPCM encoder and packing path
- SPI Flash integration references and simulation models
- Cortex-M3 software flow for recording, WAV packaging, Flash storage, and UART readback
- ModelSim testbenches for the audio and SPI building blocks

## Third-party IP notice

The original course workspace used ARM Cortex-M3 DesignStart/CMSDK and vendor/device documents. Those third-party packages and PDFs are intentionally not included in this public repository. To build the complete SoC bitstream, obtain the required ARM/Intel/Terasic materials from their official sources and place them according to the course project layout.

## Important Paths

- `soc-wuxi-arm/hw/`: audio RTL, testbenches, Quartus project metadata
- `soc-wuxi-arm/arm-soc/top.v`: top-level integration reference
- `cnasic_sleep/cnasic_sleep/App/`: firmware examples and final recording flow
- `uart_receiver.py`: PC-side UART receiver for WAV readback
