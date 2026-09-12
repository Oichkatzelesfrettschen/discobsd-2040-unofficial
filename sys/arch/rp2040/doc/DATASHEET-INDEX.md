# RP2040 datasheet, section index

Line numbers refer to `rp2040-datasheet.txt`, produced by `pdftotext -layout` from `rp2040-datasheet.pdf` (build date in the PDF metadata, 642 pages). Page numbers are the printed ones.

| Section | Title | Page | Line |
|---|---|---|---|
| 1. | Introduction | 8 |  |
| 1.1. | Why is the chip called RP2040? | 8 | 519 |
| 1.2. | Summary | 9 | 563 |
| 1.3. | The Chip | 9 | 575 |
| 1.4. | Pinout Reference | 10 | 622 |
| 1.4.1. | Pin Locations | 10 | 629 |
| 1.4.2. | Pin Descriptions | 11 | 648 |
| 1.4.3. | GPIO Functions | 12 | 707 |
| 2. | System Description | 14 |  |
| 2.1. | Bus Fabric | 14 | 842 |
| 2.1.1. | AHB-Lite Crossbar | 15 | 888 |
| 2.1.2. | Atomic Register Access | 17 | 1039 |
| 2.1.3. | APB Bridge | 17 | 1064 |
| 2.1.4. | Narrow IO Register Writes | 17 | 1079 |
| 2.1.5. | List of Registers | 18 | 1152 |
| 2.2. | Address Map | 24 | 1581 |
| 2.2.1. | Summary | 24 | 1588 |
| 2.2.2. | Detail | 24 | 1608 |
| 2.3. | Processor subsystem | 26 | 1772 |
| 2.3.1. | SIO | 27 | 1824 |
| 2.3.2. | Interrupts | 60 | 4331 |
| 2.3.3. | Event Signals | 61 | 4401 |
| 2.3.4. | Debug | 61 | 4420 |
| 2.4. | Cortex-M0+ | 62 | 4506 |
| 2.4.1. | Features | 62 | 4518 |
| 2.4.2. | Functional Description | 64 | 4595 |
| 2.4.3. | Programmer's model | 68 | 4911 |
| 2.4.4. | System control | 73 | 5316 |
| 2.4.5. | NVIC | 74 | 5385 |
| 2.4.6. | MPU | 76 | 5477 |
| 2.4.7. | Debug | 76 | 5527 |
| 2.4.8. | List of Registers | 77 | 5545 |
| 2.5. | DMA | 91 | 6607 |
| 2.5.1. | Configuring Channels | 91 | 6656 |
| 2.5.2. | Starting Channels | 93 | 6760 |
| 2.5.3. | Data Request (DREQ) | 95 | 6893 |
| 2.5.4. | Interrupts | 96 | 6989 |
| 2.5.5. | Additional Features | 96 | 7011 |
| 2.5.6. | Example Use Cases | 97 | 7092 |
| 2.5.7. | List of Registers | 101 | 7377 |
| 2.6. | Memory | 120 | 8857 |
| 2.6.1. | ROM | 120 | 8864 |
| 2.6.2. | SRAM | 121 | 8890 |
| 2.6.3. | Flash | 122 | 8977 |
| 2.7. | Boot Sequence | 128 | 9447 |
| 2.8. | Bootrom | 128 | 9467 |
| 2.8.1. | Processor Controlled Boot Sequence | 129 | 9496 |
| 2.8.2. | Launching Code On Processor Core 1 | 131 | 9625 |
| 2.8.3. | Bootrom Contents | 132 | 9681 |
| 2.8.4. | USB Mass Storage Interface | 143 | 10532 |
| 2.8.5. | USB PICOBOOT Interface | 144 | 10642 |
| 2.9. | Power Supplies | 150 | 11137 |
| 2.9.1. | Digital IO Supply (IOVDD) | 151 | 11154 |
| 2.9.2. | Digital Core Supply (DVDD) | 151 | 11174 |
| 2.9.3. | On-Chip Voltage Regulator Input Supply (VREG_VIN) | 151 | 11185 |
| 2.9.4. | USB PHY Supply (USB_VDD) | 151 | 11203 |
| 2.9.5. | ADC Supply (ADC_AVDD) | 152 | 11222 |
| 2.9.6. | Power Supply Sequencing | 152 | 11241 |
| 2.9.7. | Power Supply Schemes | 152 | 11252 |
| 2.10. | Core Supply Regulator | 155 | 11345 |
| 2.10.1. | Application Circuit | 155 | 11362 |
| 2.10.2. | Operating Modes | 156 | 11385 |
| 2.10.3. | Output Voltage Select | 157 | 11444 |
| 2.10.4. | Status | 157 | 11455 |
| 2.10.5. | Current Limit | 157 | 11471 |
| 2.10.6. | List of Registers | 157 | 11478 |
| 2.10.7. | Detailed Specifications | 160 | 11634 |
| 2.11. | Power Control | 160 | 11665 |
| 2.11.1. | Top-level Clock Gates | 160 | 11680 |
| 2.11.2. | SLEEP State | 161 | 11704 |
| 2.11.3. | DORMANT State | 161 | 11734 |
| 2.11.4. | Memory Power Down | 161 | 11753 |
| 2.11.5. | Programmer's Model | 162 | 11782 |
| 2.12. | Chip-Level Reset | 163 | 11893 |
| 2.12.1. | Overview | 163 | 11895 |
| 2.12.2. | Power-on Reset | 164 | 11924 |
| 2.12.3. | Brown-out Detection | 165 | 11975 |
| 2.12.4. | Supply Monitor | 167 | 12155 |
| 2.12.5. | External Reset | 167 | 12179 |
| 2.12.6. | Rescue Debug Port Reset | 167 | 12189 |
| 2.12.7. | Source of Last Reset | 168 | 12204 |
| 2.12.8. | List of Registers | 168 | 12214 |
| 2.13. | Power-On State Machine | 168 | 12222 |
| 2.13.1. | Overview | 168 | 12224 |
| 2.13.2. | Power On Sequence | 168 | 12241 |
| 2.13.3. | Register Control | 169 | 12312 |
| 2.13.4. | Interaction with Watchdog | 169 | 12321 |
| 2.13.5. | List of Registers | 169 | 12331 |
| 2.14. | Subsystem Resets | 172 | 12569 |
| 2.14.1. | Overview | 172 | 12571 |
| 2.14.2. | Programmer's Model | 173 | 12596 |
| 2.14.3. | List of Registers | 175 | 12776 |
| 2.15. | Clocks | 178 | 13013 |
| 2.15.1. | Overview | 178 | 13015 |
| 2.15.2. | Clock sources | 179 | 13060 |
| 2.15.3. | Clock Generators | 183 | 13291 |
| 2.15.4. | Frequency Counter | 186 | 13520 |
| 2.15.5. | Resus | 187 | 13576 |
| 2.15.6. | Programmer's Model | 187 | 13609 |
| 2.15.7. | List of Registers | 194 | 14115 |
| 2.16. | Crystal Oscillator (XOSC) | 216 | 15873 |
| 2.16.1. | Overview | 216 | 15875 |
| 2.16.2. | Usage | 217 | 15958 |
| 2.16.3. | Startup Delay | 217 | 15968 |
| 2.16.4. | XOSC Counter | 217 | 15990 |
| 2.16.5. | DORMANT mode | 218 | 16005 |
| 2.16.6. | Programmer's Model | 218 | 16049 |
| 2.16.7. | List of Registers | 219 | 16127 |
| 2.17. | Ring Oscillator (ROSC) | 221 | 16300 |
| 2.17.1. | Overview | 221 | 16302 |
| 2.17.2. | ROSC/XOSC trade-offs | 222 | 16332 |
| 2.17.3. | Modifying the frequency | 222 | 16356 |
| 2.17.4. | ROSC divider | 223 | 16394 |
| 2.17.5. | Random Number Generator | 223 | 16406 |
| 2.17.6. | ROSC Counter | 223 | 16416 |
| 2.17.7. | DORMANT mode | 223 | 16425 |
| 2.17.8. | List of Registers | 224 | 16470 |
| 2.18. | PLL | 228 | 16792 |
| 2.18.1. | Overview | 228 | 16794 |
| 2.18.2. | Calculating PLL parameters | 228 | 16833 |
| 2.18.3. | Configuration | 232 | 17093 |
| 2.18.4. | List of Registers | 234 | 17255 |
| 2.19. | GPIO | 236 | 17389 |
| 2.19.1. | Overview | 236 | 17391 |
| 2.19.2. | Function Select | 237 | 17449 |
| 2.19.3. | Interrupts | 239 | 17625 |
| 2.19.4. | Pads | 240 | 17659 |
| 2.19.5. | Software Examples | 241 | 17744 |
| 2.19.6. | List of Registers | 244 | 17989 |
| 2.20. | Sysinfo | 305 | 23002 |
| 2.20.1. | Overview | 305 | 23004 |
| 2.20.2. | List of Registers | 305 | 23011 |
| 2.21. | Syscfg | 306 | 23083 |
| 2.21.1. | Overview | 306 | 23085 |
| 2.21.2. | List of Registers | 306 | 23105 |
| 2.22. | TBMAN | 309 | 23322 |
| 2.22.1. | List of Registers | 309 | 23331 |
| 3. | PIO | 311 |  |
| 3.1. | Overview | 311 | 23377 |
| 3.2. | Programmer's Model | 312 | 23457 |
| 3.2.1. | PIO Programs | 312 | 23487 |
| 3.2.2. | Control Flow | 313 | 23532 |
| 3.2.3. | Registers | 314 | 23642 |
| 3.2.4. | Stalling | 317 | 23842 |
| 3.2.5. | Pin Mapping | 318 | 23870 |
| 3.2.6. | IRQ Flags | 318 | 23892 |
| 3.2.7. | Interactions Between State Machines | 318 | 23908 |
| 3.3. | PIO Assembler (pioasm) | 319 | 23932 |
| 3.3.1. | Directives | 319 | 23944 |
| 3.3.2. | Values | 320 | 24003 |
| 3.3.3. | Expressions | 320 | 24024 |
| 3.3.4. | Comments | 320 | 24046 |
| 3.3.5. | Labels | 320 | 24054 |
| 3.3.6. | Instructions | 321 | 24082 |
| 3.3.7. | Pseudoinstructions | 321 | 24121 |
| 3.4. | Instruction Set | 321 | 24130 |
| 3.4.1. | Summary | 321 | 24132 |
| 3.4.2. | JMP | 322 | 24177 |
| 3.4.3. | WAIT | 323 | 24247 |
| 3.4.4. | IN | 324 | 24329 |
| 3.4.5. | OUT | 325 | 24393 |
| 3.4.6. | PUSH | 326 | 24460 |
| 3.4.7. | PULL | 327 | 24518 |
| 3.4.8. | MOV | 328 | 24586 |
| 3.4.9. | IRQ | 329 | 24671 |
| 3.4.10. | SET | 330 | 24743 |
| 3.5. | Functional Details | 331 | 24797 |
| 3.5.1. | Side-set | 331 | 24799 |
| 3.5.2. | Program Wrapping | 332 | 24876 |
| 3.5.3. | FIFO Joining | 334 | 25026 |
| 3.5.4. | Autopush and Autopull | 335 | 25082 |
| 3.5.5. | Clock Dividers | 339 | 25406 |
| 3.5.6. | GPIO Mapping | 340 | 25495 |
| 3.5.7. | Forced and EXEC'd Instructions | 342 | 25631 |
| 3.6. | Examples | 344 | 25757 |
| 3.6.1. | Duplex SPI | 344 | 25770 |
| 3.6.2. | WS2812 LEDs | 348 | 26034 |
| 3.6.3. | UART TX | 350 | 26154 |
| 3.6.4. | UART RX | 352 | 26341 |
| 3.6.5. | Manchester Serial TX and RX | 355 | 26574 |
| 3.6.6. | Differential Manchester (BMC) TX and RX | 357 | 26761 |
| 3.6.7. | I2C | 361 | 27002 |
| 3.6.8. | PWM | 364 | 27300 |
| 3.6.9. | Addition | 366 | 27451 |
| 3.6.10. | Further Examples | 367 | 27537 |
| 3.7. | List of Registers | 368 | 27560 |
| 4. | Peripherals | 383 |  |
| 4.1. | USB | 383 | 28690 |
| 4.1.1. | Overview | 383 | 28692 |
| 4.1.2. | Architecture | 384 | 28745 |
| 4.1.3. | Programmer's Model | 394 | 29527 |
| 4.1.4. | List of Registers | 398 | 29822 |
| 4.2. | UART | 417 | 31287 |
| 4.2.1. | Overview | 417 | 31318 |
| 4.2.2. | Functional description | 418 | 31365 |
| 4.2.3. | Operation | 420 | 31464 |
| 4.2.4. | UART hardware flow control | 422 | 31636 |
| 4.2.5. | UART DMA Interface | 424 | 31722 |
| 4.2.6. | Interrupts | 425 | 31829 |
| 4.2.7. | Programmer's Model | 427 | 31948 |
| 4.2.8. | List of Registers | 429 | 32085 |
| 4.3. | I2C | 440 | 32905 |
| 4.3.1. | Features | 440 | 32920 |
| 4.3.2. | IP Configuration | 441 | 32973 |
| 4.3.3. | I2C Overview | 441 | 32997 |
| 4.3.4. | I2C Terminology | 443 | 33110 |
| 4.3.5. | I2C Behaviour | 444 | 33168 |
| 4.3.6. | I2C Protocols | 445 | 33260 |
| 4.3.7. | Tx FIFO Management and START, STOP and RESTART Generation | 448 | 33529 |
| 4.3.8. | Multiple Master Arbitration | 450 | 33779 |
| 4.3.9. | Clock Synchronization | 451 | 33843 |
| 4.3.10. | Operation Modes | 452 | 33878 |
| 4.3.11. | Spike Suppression | 457 | 34243 |
| 4.3.12. | Fast Mode Plus Operation | 458 | 34318 |
| 4.3.13. | Bus Clear Feature | 458 | 34334 |
| 4.3.14. | IC_CLK Frequency Configuration | 459 | 34397 |
| 4.3.15. | DMA Controller Interface | 463 | 34700 |
| 4.3.16. | Operation of Interrupt Registers | 464 | 34750 |
| 4.3.17. | List of Registers | 464 | 34787 |
| 4.4. | SPI | 501 | 37609 |
| 4.4.1. | Overview | 502 | 37660 |
| 4.4.2. | Functional Description | 502 | 37692 |
| 4.4.3. | Operation | 505 | 37867 |
| 4.4.4. | List of Registers | 515 | 38697 |
| 4.5. | PWM | 521 | 39166 |
| 4.5.1. | Overview | 521 | 39168 |
| 4.5.2. | Programmer's Model | 522 | 39224 |
| 4.5.3. | List of Registers | 529 | 39792 |
| 4.6. | Timer | 534 | 40211 |
| 4.6.1. | Overview | 534 | 40213 |
| 4.6.2. | Counter | 535 | 40252 |
| 4.6.3. | Alarms | 535 | 40271 |
| 4.6.4. | Programmer's Model | 536 | 40307 |
| 4.6.5. | List of Registers | 539 | 40579 |
| 4.7. | Watchdog | 544 | 40941 |
| 4.7.1. | Overview | 544 | 40943 |
| 4.7.2. | Tick generation | 544 | 40956 |
| 4.7.3. | Watchdog Counter | 545 | 40989 |
| 4.7.4. | Scratch Registers | 545 | 41001 |
| 4.7.5. | Programmer's Model | 545 | 41012 |
| 4.7.6. | List of Registers | 547 | 41128 |
| 4.8. | RTC | 548 | 41264 |
| 4.8.1. | Storage Format | 548 | 41271 |
| 4.8.2. | Leap year | 549 | 41317 |
| 4.8.3. | Interrupts | 549 | 41331 |
| 4.8.4. | Reference clock | 549 | 41342 |
| 4.8.5. | Programmer's Model | 550 | 41381 |
| 4.8.6. | List of Registers | 553 | 41611 |
| 4.9. | ADC and Temperature Sensor | 557 | 41950 |
| 4.9.1. | ADC controller | 558 | 41988 |
| 4.9.2. | SAR ADC | 559 | 42007 |
| 4.9.3. | ADC ENOB | 561 | 42191 |
| 4.9.4. | INL and DNL | 562 | 42258 |
| 4.9.5. | Temperature Sensor | 563 | 42304 |
| 4.9.6. | List of Registers | 564 | 42341 |
| 4.10. | SSI | 567 | 42607 |
| 4.10.1. | Overview | 568 | 42627 |
| 4.10.2. | Features | 568 | 42688 |
| 4.10.3. | IP Modifications | 569 | 42725 |
| 4.10.4. | Clock Ratios | 570 | 42826 |
| 4.10.5. | Transmit and Receive FIFO Buffers | 571 | 42875 |
| 4.10.7. | SSI Interrupts | 572 | 42973 |
| 4.10.8. | Transfer Modes | 573 | 43029 |
| 4.10.9. | Operation Modes | 574 | 43107 |
| 4.10.10. | Partner Connection Interfaces | 579 | 43528 |
| 4.10.11. | DMA Controller Interface | 595 | 44853 |
| 4.10.12. | APB Interface | 597 | 45033 |
| 4.10.13. | List of Registers | 598 | 45074 |
| 5. | Electrical and Mechanical | 607 |  |
| 5.1. | Package | 607 | 45769 |
| 5.1.1. | Thermal characteristics | 608 | 45810 |
| 5.1.2. | Recommended PCB Footprint | 608 | 45822 |
| 5.1.3. | Package markings | 608 | 45857 |
| 5.2. | Storage conditions | 609 | 45901 |
| 5.3. | Solder profile | 609 | 45908 |
| 5.4. | Compliance | 611 | 46007 |
| 5.5. | Pinout | 611 | 46029 |
| 5.5.1. | Pin Locations | 611 | 46031 |
| 5.5.2. | Pin Definitions | 612 | 46045 |
| 5.5.3. | Pin Specifications | 614 | 46254 |
| 5.6. | Power Supplies | 622 | 46827 |
| 5.7. | Power Consumption | 622 | 46854 |
| 5.7.1. | Peripheral power consumption | 622 | 46856 |
| 5.7.2. | Power consumption for typical user cases | 623 | 46923 |
