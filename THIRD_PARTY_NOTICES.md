# Third-party notices

RaftCore is MIT licensed (see [LICENSE](LICENSE)). It also contains code, and device type records with settings or
formulas, from other sources. This file lists them all, with the licence of each, and gives the notices those licences
ask to be kept. The longer licences are in [LICENSES](LICENSES).

Firmware built with RaftCore includes this code, and the records it compiles in. Most of these licences ask for their
notice to go with a product's documentation as well as with source code, so a product that ships firmware built with
RaftCore should include this file (or the entries for what it uses) with its documentation, along with the notices of
the other libraries it is built with (ESP-IDF's are listed in its `docs/en/COPYRIGHT.rst`).

## Code

| Files | Source | Licence |
|-|-|-|
| `components/core/ArduinoUtils/ArduinoWString.cpp`, `.h` (the `String` class) | The Wiring/Arduino `WString` (Hernando Barragan, Paul Stoffregen), with the esp8266 Arduino core's changes (Ivan Grokhotkov, Michael C. Miller) | LGPL-2.1 or later ([LICENSES/LGPL-2.1.txt](LICENSES/LGPL-2.1.txt)) |
| `components/core/ArduinoUtils/ArduinoStdlibNonISO.c`, `.h` | The esp8266 Arduino core's `core_esp8266_noniso.c` (Ivan Grokhotkov, changed by Markus Sattler) | LGPL-2.1 or later |
| `components/core/ArduinoUtils/ArduinoPgmSpace.h` | The RaspberryPi Arduino core (Hristo Gochkov) | LGPL-2.1 or later |
| `components/core/ArPreferences` | Espressif's `Preferences` (arduino-esp32), Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD | Apache-2.0 ([LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt)) |
| `components/core/LEDPixels/LEDStripEncoder.c`, `.h` | ESP-IDF's RMT LED strip example, Copyright 2021-2022 Espressif Systems (Shanghai) CO LTD | Apache-2.0 |
| `components/core/ExpressionEval/tinyexpr.c`, `.h` | [TinyExpr](https://github.com/codeplea/tinyexpr), Lewis Van Winkle | zlib, below |
| `components/core/Utils/PlatformUtils.cpp` (`strlcat`) | OpenBSD's `strlcat.c`, Todd C. Miller | ISC, below |
| `components/core/LEDPixels/LEDPixHSV.h` (HSV to RGB) | [fast_hsv2rgb](https://www.vagrearg.org/content/hsvrgb), B. Stultiens | MIT, below |
| `components/core/libb64` | [libb64](http://sourceforge.net/projects/libb64) | Public domain |
| `components/core/NumericalFilters/SimpleMovingAverage.h`, `ExpMovingAverage.h`, `AngleMovingAverage.h` | Pieter P (tttapa)'s filter examples on [tttapa.github.io](https://tttapa.github.io/Pages/Mathematics/Systems-and-Control-Theory/Digital-filters/), whose repository is GPL-3.0 | GPL-3.0 (see the note below) |
| `components/core/MiniHDLC` | Design only, "very loosely based on" [minihdlc](https://github.com/mengguang/minihdlc) (Apache-2.0); no code copied | Cited |
| `components/core/NetworkSystem` | Based on ESP-IDF's WiFi station example, which is public domain or CC0 | No notice needed |

**Note on the moving average filters:** code from a GPL-3.0 source can't be part of an MIT library without making the
firmware it goes into GPL-3.0. These three filters need replacing with code written for RaftCore.

**Note on the LGPL files:** the LGPL-2.1 applies to those files, not to the rest of RaftCore. Firmware links them
statically, so a product that ships it must let its users relink the firmware with a changed version of those files -
for example by providing the firmware's object files or its source - and must say that the files are used, under the
LGPL, with a copy of the licence.

## Device type records

Some records in [devtypes/records](devtypes/records) use register sequences, settings or formulas from third-party
drivers and documents. Each record's `_notes` names its source. The records are compiled into firmware that selects
them, and a device type file built from them carries them too (without their notes).

| Record | Taken from | Licence |
|-|-|-|
| VL53L0X | Init (DataInit, StaticInit with the reference SPADs and ST's default tuning settings, the timing budget, the reference calibrations) and continuous ranging, as Pololu's [vl53l0x-arduino](https://github.com/pololu/vl53l0x-arduino) does them, which is based on ST's VL53L0X API (STSW-IMG005) | Pololu: MIT. The parts from ST's API: BSD-3-Clause (ST). Both below |
| VL53L1X | The default configuration, the init sequence (SensorInit), the timing budget values and the range status codes from ST's VL53L1X Ultra Lite Driver (STSW-IMG009, as packaged in SparkFun's [VL53L1X library](https://github.com/sparkfun/SparkFun_VL53L1X_Arduino_Library) `src/st_src`) | BSD-3-Clause (ST), below |
| VL53L4CD | The default configuration, the init and timing sequence (SensorInit, SetRangeTiming) and the range status codes from ST's VL53L4CD Ultra Lite Driver (as packaged by ST in [stm32duino/VL53L4CD](https://github.com/stm32duino/VL53L4CD) `src/vl53l4cd_api.cpp`) | BSD-3-Clause (ST), below |
| TSL2591 | The lux calculation and its coefficients (`calculateLux`) from Adafruit's [TSL2591 library](https://github.com/adafruit/Adafruit_TSL2591_Library) | BSD-3-Clause (Adafruit), below |
| TCS34725 | The colour temperature calculation (`calculateColorTemperature_dn40`, after ams's design note DN40) and its saturation check, from Adafruit's [TCS34725 library](https://github.com/adafruit/Adafruit_TCS34725); the lux formula from ams's DN40 | BSD-3-Clause (Adafruit), below; DN40 cited |
| AS7341 | The SMUX configurations (`setup_F1F4_Clear_NIR`, `setup_F5F8_Clear_NIR`), the measurement sequence, the LED control (`enableLED`, `setLEDCurrent`) and the ID check, from Adafruit's [AS7341 library](https://github.com/adafruit/Adafruit_AS7341) | BSD-3-Clause (Adafruit), below |
| BME690 | The compensation formulas and the heater set-point calculation (`calc_res_heat`) from Bosch's [BME690_SensorAPI](https://github.com/boschsensortec/BME690_SensorAPI) (`bme69x.c` v1.1.0) | BSD-3-Clause (Bosch Sensortec), below |
| BME680 | The compensation formulas and the heater set-point calculation (`calc_res_heat`), as in the BME680 datasheet and Bosch's [BME68x_SensorAPI](https://github.com/boschsensortec/BME68x_SensorAPI) | BSD-3-Clause (Bosch Sensortec), below |
| VL6180 | The private register settings from ST's application note [AN4545](https://www.st.com/resource/en/application_note/an4545-vl6180x-basic-ranging-application-note-stmicroelectronics.pdf) (VL6180X basic ranging) | ST document; cited |
| TrillBar, TrillSquare, TrillCraft, TrillRing, TrillHex, TrillFlex | Each sensor's default mode, prescaler and noise threshold, from the defaults table of Bela's Trill library ([Bela](https://github.com/BelaPlatform/Bela) `libraries/Trill`, LGPL-3.0; also [Trill-Linux](https://github.com/BelaPlatform/Trill-Linux)). Only these values and the sensors' documented commands are used, not the library's code | Cited |

Other records cite datasheets, or name a board maker (e.g. a board's default address); those need no notice.

---

## Notices

### TinyExpr (zlib)

TINYEXPR - Tiny recursive descent parser and evaluation engine in C

Copyright (c) 2015, 2016 Lewis Van Winkle

http://CodePlea.com

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgement in the product documentation would be
appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

### strlcat (ISC)

Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

### fast_hsv2rgb (MIT)

The MIT License (MIT)

Copyright (c) 2016  B. Stultiens

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.

### Pololu vl53l0x-arduino (VL53L0X record; MIT)

Copyright (c) 2017-2022 Pololu Corporation.  For more information, see

https://www.pololu.com/
https://forum.pololu.com/

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

### STMicroelectronics VL53L0X API (VL53L0X record, through Pololu's vl53l0x-arduino; BSD-3-Clause)

Copyright © 2016, STMicroelectronics International N.V.  All
rights reserved.

Redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following
conditions are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided
with the distribution.
* Neither the name of STMicroelectronics nor the
names of its contributors may be used to endorse or promote
products derived from this software without specific prior
written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
NON-INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS ARE DISCLAIMED.
IN NO EVENT SHALL STMICROELECTRONICS INTERNATIONAL N.V. BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

### STMicroelectronics VL53L1X Ultra Lite Driver (VL53L1X record; BSD-3-Clause)

COPYRIGHT(c) 2018 STMicroelectronics

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
  3. Neither the name of STMicroelectronics nor the names of its contributors
     may be used to endorse or promote products derived from this software
     without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### STMicroelectronics VL53L4CD Ultra Lite Driver (VL53L4CD record; BSD-3-Clause)

COPYRIGHT(c) 2021 STMicroelectronics

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
  3. Neither the name of STMicroelectronics nor the names of its contributors
     may be used to endorse or promote products derived from this software
     without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Adafruit TSL2591 library (TSL2591 record; BSD-3-Clause)

Copyright (c) 2014 Adafruit Industries
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holders nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Adafruit TCS34725 library (TCS34725 record; BSD-3-Clause)

Software License Agreement (BSD License)

Copyright (c) 2012, Adafruit Industries
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holders nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Adafruit AS7341 library (AS7341 record; BSD-3-Clause)

Software License Agreement (BSD License)

Copyright (c) 2019 Bryan Siepert for Adafruit Industries
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holders nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Bosch Sensortec BME690 and BME68x SensorAPI (BME690 and BME680 records; BSD-3-Clause)

Copyright (c) 2024 Bosch Sensortec GmbH. All rights reserved. (BME690_SensorAPI)

Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved. (BME68x_SensorAPI)

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

### Espressif (Apache-2.0)

Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD (ArPreferences). Copyright 2021-2022 Espressif Systems
(Shanghai) CO LTD (LEDStripEncoder). Licensed under the Apache License, Version 2.0:
[LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt).

### Arduino core files (LGPL-2.1 or later)

Copyright (c) 2009-10 Hernando Barragan. Copyright 2011, Paul Stoffregen. Modified by Ivan Grokhotkov, 2014, and
Michael C. Miller, 2015 (ArduinoWString). Copyright (c) 2014 Ivan Grokhotkov, modified 03 April 2015 by Markus Sattler
(ArduinoStdlibNonISO). Copyright (c) 2015 Hristo Gochkov (ArduinoPgmSpace). These files are free software; you can
redistribute them and/or modify them under the terms of the GNU Lesser General Public License as published by the Free
Software Foundation; either version 2.1 of the License, or (at your option) any later version:
[LICENSES/LGPL-2.1.txt](LICENSES/LGPL-2.1.txt).
