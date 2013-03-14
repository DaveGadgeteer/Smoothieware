/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "analogin_api.h"

#if DEVICE_ANALOGIN

#include "cmsis.h"
#include "pinmap.h"
#include "error.h"

#define ANALOGIN_MEDIAN_FILTER      1

#define ADC_10BIT_RANGE             0x3FF
#define ADC_12BIT_RANGE             0xFFF

static inline int div_round_up(int x, int y) {
  return (x + (y - 1)) / y;
}

#if defined(TARGET_LPC1768) || defined(TARGET_LPC2368)
static const PinMap PinMap_ADC[] = {
    {P0_23, ADC0_0, 1},
    {P0_24, ADC0_1, 1},
    {P0_25, ADC0_2, 1},
    {P0_26, ADC0_3, 1},
    {P1_30, ADC0_4, 3},
    {P1_31, ADC0_5, 3},
#ifdef TARGET_LPC1768
    {P0_2,  ADC0_7, 2},
    {P0_3,  ADC0_6, 2},
#endif
    {NC,    NC,     0}
};

#if defined(TARGET_LPC2368)
#   define ADC_RANGE    ADC_10BIT_RANGE
#elif defined(TARGET_LPC1768)
#   define ADC_RANGE    ADC_12BIT_RANGE
#endif

#elif defined(TARGET_LPC11U24)
static const PinMap PinMap_ADC[] = {
    {P0_11, ADC0_0, 0x02},
    {P0_12, ADC0_1, 0x02},
    {P0_13, ADC0_2, 0x02},
    {P0_14, ADC0_3, 0x02},
    {P0_15, ADC0_4, 0x02},
    {P0_16, ADC0_5, 0x01},
    {P0_22, ADC0_6, 0x01},
    {P0_23, ADC0_7, 0x01},
    {NC   , NC    , 0   }
};

#define LPC_IOCON0_BASE (LPC_IOCON_BASE)
#define LPC_IOCON1_BASE (LPC_IOCON_BASE + 0x60)

#define ADC_RANGE    ADC_10BIT_RANGE
#endif

void analogin_init(analogin_t *obj, PinName pin) {
    obj->adc = (ADCName)pinmap_peripheral(pin, PinMap_ADC);
    if (obj->adc == (uint32_t)NC) {
        error("ADC pin mapping failed");
    }

#if defined(TARGET_LPC1768) || defined(TARGET_LPC2368)
    // ensure power is turned on
    LPC_SC->PCONP |= (1 << 12);

    // set PCLK of ADC to /1
    LPC_SC->PCLKSEL0 &= ~(0x3 << 24);
    LPC_SC->PCLKSEL0 |= (0x1 << 24);
    uint32_t PCLK = SystemCoreClock;

    // calculate minimum clock divider
    //  clkdiv = divider - 1
    uint32_t MAX_ADC_CLK = 13000000;
    uint32_t clkdiv = div_round_up(PCLK, MAX_ADC_CLK) - 1;

    // Set the generic software-controlled ADC settings
    LPC_ADC->ADCR = (0 << 0)      // SEL: 0 = no channels selected
                  | (clkdiv << 8) // CLKDIV: PCLK max ~= 25MHz, /25 to give safe 1MHz at fastest
                  | (0 << 16)     // BURST: 0 = software control
                  | (0 << 17)     // CLKS: not applicable
                  | (1 << 21)     // PDN: 1 = operational
                  | (0 << 24)     // START: 0 = no start
                  | (0 << 27);    // EDGE: not applicable

#elif defined(TARGET_LPC11U24)
    // Power up ADC
    LPC_SYSCON->PDRUNCFG &= ~ (1 << 4);
    LPC_SYSCON->SYSAHBCLKCTRL |= ((uint32_t)1 << 13);

    uint32_t pin_number = (uint32_t)pin;
    __IO uint32_t *reg = (pin_number < 32) ? (__IO uint32_t*)(LPC_IOCON0_BASE + 4 * pin_number) : (__IO uint32_t*)(LPC_IOCON1_BASE + 4 * (pin_number - 32));

    // set pin to ADC mode
    *reg &= ~(1 << 7); // set ADMODE = 0 (analog mode)

    uint32_t PCLK = SystemCoreClock;
    uint32_t MAX_ADC_CLK = 4500000;
    uint32_t clkdiv = div_round_up(PCLK, MAX_ADC_CLK) - 1;

    LPC_ADC->CR = (0 << 0)      // no channels selected
                | (clkdiv << 8) // max of 4.5MHz
                | (0 << 16)     // BURST = 0, software controlled
                | ( 0 << 17 );  // CLKS = 0, not applicable
#endif
    pinmap_pinout(pin, PinMap_ADC);
}

static inline uint32_t adc_read(analogin_t *obj) {
#if defined(TARGET_LPC1768) || defined(TARGET_LPC2368)
    // Select the appropriate channel and start conversion
    LPC_ADC->ADCR &= ~0xFF;
    LPC_ADC->ADCR |= 1 << (int)obj->adc;
    LPC_ADC->ADCR |= 1 << 24;

    // Repeatedly get the sample data until DONE bit
    unsigned int data;
    do {
        data = LPC_ADC->ADGDR;
    } while ((data & ((unsigned int)1 << 31)) == 0);

    // Stop conversion
    LPC_ADC->ADCR &= ~(1 << 24);

#elif defined(TARGET_LPC11U24)
    // Select the appropriate channel and start conversion
    LPC_ADC->CR &= ~0xFF;
    LPC_ADC->CR |= 1 << (int)obj->adc;
    LPC_ADC->CR |= 1 << 24;

    // Repeatedly get the sample data until DONE bit
    unsigned int data;
    do {
        data = LPC_ADC->GDR;
    } while ((data & ((unsigned int)1 << 31)) == 0);

    // Stop conversion
    LPC_ADC->CR &= ~(1 << 24);
#endif

#if defined(TARGET_LPC1768)
    return (data >> 4) & ADC_RANGE; // 12 bit
#elif defined(TARGET_LPC2368) || defined (TARGET_LPC11U24)
    return (data >> 6) & ADC_RANGE; // 10 bit
#endif
}

static inline void order(uint32_t *a, uint32_t *b) {
    if (*a > *b) {
        uint32_t t = *a;
        *a = *b;
        *b = t;
    }
}

static inline uint32_t adc_read_u32(analogin_t *obj) {
    uint32_t value;
#if ANALOGIN_MEDIAN_FILTER
    uint32_t v1 = adc_read(obj);
    uint32_t v2 = adc_read(obj);
    uint32_t v3 = adc_read(obj);
    order(&v1, &v2);
    order(&v2, &v3);
    order(&v1, &v2);
    value = v2;
#else
    value = adc_read(adc);
#endif
    return value;
}

uint16_t analogin_read_u16(analogin_t *obj) {
    uint32_t value = adc_read_u32(obj);

#if defined(TARGET_LPC1768)
    return (value << 4) | ((value >> 8) & 0x000F); // 12 bit
#elif defined(TARGET_LPC2368) || defined(TARGET_LPC11U24)
    return (value << 6) | ((value >> 4) & 0x003F); // 10 bit
#endif
}

float analogin_read(analogin_t *obj) {
    uint32_t value = adc_read_u32(obj);
    return (float)value * (1.0f / (float)ADC_RANGE);
}

#endif