# Third-party notices

RaftCore is MIT licensed (see [LICENSE](LICENSE)). Some device type records in [devtypes/records](devtypes/records) use
register sequences, settings or formulas taken from third-party drivers and documents. Each record's `_notes` names
its source. Where the source's licence asks for its notice to be kept, the notice is below. That includes firmware
built with these records (the records' init strings and code are compiled into it), so a product that ships such
firmware should include this file, or the notices for the records it uses, with its documentation.

| Record | Taken from | Licence |
|-|-|-|
| VL53L0X | Init (DataInit, StaticInit with the reference SPADs and ST's default tuning settings, the timing budget, the reference calibrations) and continuous ranging, as Pololu's [vl53l0x-arduino](https://github.com/pololu/vl53l0x-arduino) does them, which is based on ST's VL53L0X API (STSW-IMG005) | Pololu: MIT. The parts from ST's API: BSD-3-Clause (ST). Both below |
| BME690 | The compensation formulas and the heater set-point calculation (`calc_res_heat`) from Bosch's [BME690_SensorAPI](https://github.com/boschsensortec/BME690_SensorAPI) (`bme69x.c` v1.1.0) | BSD-3-Clause (Bosch Sensortec), below |
| BME680 | The compensation formulas and the heater set-point calculation (`calc_res_heat`), as in the BME680 datasheet and Bosch's [BME68x_SensorAPI](https://github.com/boschsensortec/BME68x_SensorAPI) | BSD-3-Clause (Bosch Sensortec), below |
| VL6180 | The private register settings from ST's application note [AN4545](https://www.st.com/resource/en/application_note/an4545-vl6180x-basic-ranging-application-note-stmicroelectronics.pdf) (VL6180X basic ranging) | ST document; cited, no licence text |
| TrillBar, TrillSquare, TrillCraft, TrillRing, TrillHex, TrillFlex | Each sensor's default mode, prescaler and noise threshold, from the defaults table of Bela's Trill library ([Bela](https://github.com/BelaPlatform/Bela) `libraries/Trill`, LGPL-3.0; also [Trill-Linux](https://github.com/BelaPlatform/Trill-Linux)). Only these values and the sensors' documented commands are used, not the library's code | Cited; no code is copied |

Other records cite datasheets, or name a board maker (e.g. a board's default address); those need no notice.

---

## Pololu vl53l0x-arduino (VL53L0X record)

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

## STMicroelectronics VL53L0X API (VL53L0X record, through Pololu's vl53l0x-arduino)

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

## Bosch Sensortec BME690 and BME68x SensorAPI (BME690 and BME680 records)

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
