ADC2 and Wi-Fi Conflict
There is a major limitation regarding analog functions: ADC2 channels cannot be used simultaneously with Wi-Fi.
• The pins you have assigned to your nRF24 and CC1101 modules (GPIO 11, 12, 13, 14, 16, 17, and 18) are all part of the ADC2 subsystem.
• While these pins work fine for digital SPI communication, you cannot use them for any analog sensing tasks if the Wi-Fi radio is active.
3. Strapping Pin Dependencies
Certain pins act as "strapping pins" that the chip samples during the start-up sequence to determine its boot configuration.
• GPIO 3 (your ANALOG_PIN): This pin controls the JTAG signal source at boot. It is "floating" by default and must be handled carefully to avoid accidentally entering a debug mode that could block your code execution.
• GPIO 0, 45, and 46: These control critical settings like Joint Download Boot and the internal VDD_SPI voltage. If your external attachments (like the LoRa or PN532) pull these pins high or low at the wrong time during a reset, the board may fail to boot or may provide the wrong voltage to the internal flash memory, potentially damaging it.
4. Hardwired Hardware Conflicts
The development board has several pins permanently tied to onboard components that will interfere with general usage:
• Onboard LEDs: GPIO 43 (TX LED) and GPIO 44 (RX LED) are hardwired to blink during serial data transmission. If you use these for your radios, the onboard LEDs will blink constantly, and the electrical load of the LEDs might interfere with high-speed signals.
• USB Controller: GPIO 19 and 20 are connected to the internal USB Serial/JTAG controller by default. To use them as general-purpose pins for your radios, you must explicitly reconfigure them in software to disable the USB function.
5. Bus Speed and resolving Capacity
• I2C Limit: Your I2C bus (GPIO 7, 8) is limited to a maximum speed of 800 kbit/s, but this is strictly dependent on having strong enough pull-up resistors on your SDA and SCL lines.
• SPI Master Limit: The SPI2 bus (FSPI) you are using for the radio modules supports a maximum clock frequency of 80 MHz. Trying to push the nRF24 or LoRa modules beyond this frequency will result in data corruption.