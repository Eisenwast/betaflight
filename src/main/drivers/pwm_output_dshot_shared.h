/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef USE_DSHOT

static FAST_RAM_ZERO_INIT uint8_t dmaMotorTimerCount = 0;
static FAST_RAM_ZERO_INIT motorDmaTimer_t dmaMotorTimers[MAX_DMA_TIMERS];
static FAST_RAM_ZERO_INIT motorDmaOutput_t dmaMotors[MAX_SUPPORTED_MOTORS];

#ifdef USE_DSHOT_TELEMETRY
FAST_RAM_ZERO_INIT uint32_t readDoneCount;

// TODO remove once debugging no longer needed
FAST_RAM_ZERO_INIT uint32_t dshotInvalidPacketCount;
FAST_RAM_ZERO_INIT uint32_t  inputBuffer[DSHOT_TELEMETRY_INPUT_LEN];
FAST_RAM_ZERO_INIT uint32_t setDirectionMicros;
#endif


motorDmaOutput_t *getMotorDmaOutput(uint8_t index)
{
    return &dmaMotors[index];
}

uint8_t getTimerIndex(TIM_TypeDef *timer)
{
    for (int i = 0; i < dmaMotorTimerCount; i++) {
        if (dmaMotorTimers[i].timer == timer) {
            return i;
        }
    }
    dmaMotorTimers[dmaMotorTimerCount++].timer = timer;
    return dmaMotorTimerCount - 1;
}

#ifdef USE_DSHOT_TELEMETRY

static void enableChannels(uint8_t motorCount);

static uint16_t decodeDshotPacket(uint32_t buffer[])
{
    uint32_t value = 0;
    for (int i = 1; i < DSHOT_TELEMETRY_INPUT_LEN; i += 2) {
        int diff = buffer[i] - buffer[i-1];
        value <<= 1;
        if (diff > 0) {
            if (diff >= 11) value |= 1;
        } else {
            if (diff >= -9) value |= 1;
        }
    }

    uint32_t csum = value;
    csum = csum ^ (csum >> 8); // xor bytes
    csum = csum ^ (csum >> 4); // xor nibbles

    if (csum & 0xf) {
        return 0xffff;
    }
    return value >> 4;
}

static uint16_t decodeProshotPacket(uint32_t buffer[])
{
    uint32_t value = 0;
    for (int i = 1; i < PROSHOT_TELEMETRY_INPUT_LEN; i += 2) {
        const int proshotModulo = MOTOR_NIBBLE_LENGTH_PROSHOT;
        int diff = ((buffer[i] + proshotModulo - buffer[i-1]) % proshotModulo) - PROSHOT_BASE_SYMBOL;
        int nibble;
        if (diff < 0) {
            nibble = 0;
        } else {
            nibble = (diff + PROSHOT_BIT_WIDTH / 2) / PROSHOT_BIT_WIDTH;
        }
        value <<= 4;
        value |= (nibble & 0xf);
    }

    uint32_t csum = value;
    csum = csum ^ (csum >> 8); // xor bytes
    csum = csum ^ (csum >> 4); // xor nibbles

    if (csum & 0xf) {
        return 0xffff;
    }
    return value >> 4;
}

#endif


#ifdef USE_DSHOT_TELEMETRY

uint16_t getDshotTelemetry(uint8_t index)
{
    return dmaMotors[index].dshotTelemetryValue;
}

inline FAST_CODE static void pwmDshotSetDirectionOutput(
    motorDmaOutput_t * const motor, bool output
#ifndef USE_DSHOT_TELEMETRY
    , LL_TIM_OC_InitTypeDef* pOcInit, LL_DMA_InitTypeDef* pDmaInit)
#endif
);

void pwmStartDshotMotorUpdate(uint8_t motorCount)
{
    if (useDshotTelemetry) {
        for (int i = 0; i < motorCount; i++) {
            if (dmaMotors[i].hasTelemetry) {
#ifdef STM32F7
                uint32_t edges = LL_EX_DMA_GetDataLength(dmaMotors[i].dmaRef);
#else
                uint32_t edges = DMA_GetCurrDataCounter(motor->dmaRef);
#endif
                uint16_t value = 0xffff;
                if (edges == 0) {
                    if (dmaMotors[i].useProshot) {
                        value = decodeProshotPacket(dmaMotors[i].dmaBuffer);
                    } else {
                        value = decodeDshotPacket(dmaMotors[i].dmaBuffer);
                    }
                }
                if (value != 0xffff) {
                    dmaMotors[i].dshotTelemetryValue = value;
                    if (i < 4) {
                        DEBUG_SET(DEBUG_DSHOT_RPM_TELEMETRY, i, value);
                    }
                } else {
                    dshotInvalidPacketCount++;
                    if (i == 0) {
                        memcpy(inputBuffer,dmaMotors[i].dmaBuffer,sizeof(inputBuffer));
                    }
                }
                dmaMotors[i].hasTelemetry = false;
            } else {
#ifdef STM32F7
                LL_EX_TIM_DisableIT(dmaMotors[i].timerHardware->tim, dmaMotors[i].timerDmaSource);
#else
                TIM_DMACmd(dmaMotors[i].timerHardware->tim, dmaMotors[i].timerDmaSource, DISABLE);
#endif
            }
            pwmDshotSetDirectionOutput(&dmaMotors[i], true);
        }
        enableChannels(motorCount);
    }
}

#endif
#endif
