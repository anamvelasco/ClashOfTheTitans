/**
 * @file main.c
 *
 * @mainpage Signal Generator Project
 *
 * @section description Description
 * This program turns the Raspberry Pi Pico (RP2040) into a programmable signal generator.
 * The code includes support to input peripherals such as a push-button or a 4x4 Keypad.
 * The output is given as 8-bits that should be fed to an 8-bit DAC, in order to obtain 
 * an analogue signal. The push-button is used to change between waveformes (sine, square, sawtooth and triangle), 
 * while 4x4 Keypad can be used to introduce the desired amplitude, frequency and offset values.
 * In this case. the program implements both INTERRUPT and POLLING methods, to handle pushbutton and Keypad inputs respectively.
 *
 * @section circuit Circuit
 * - Push-button connected to GP16
 * - Keypad Rows connected like    (r1, r2, r3, r4) to (GP18, GP19, GP20, GP21) . Could be changed from here, as long as initialization is this case does NOT required consecutive pins
 * - Keypad columns connected like (c1, c2, c3, c4) to (GP22, GP26, GP27, GP28) . Could be changed from here, as long as initialization is this case does NOT required consecutive pins
 *
 * @section libraries Libraries
 * - math.h (https://pubs.opengroup.org/onlinepubs/009695399/basedefs/math.h.html)
 *   - Useful to perform mathematical operations, necessary to build function equations.
 * - string.h (https://www.ibm.com/docs/es/i/7.5?topic=files-stringh)
 *   - Necessary to process strings such as the input introduced by the Keypad. mainly used to convert strings into float or any numeric representation.
 * - time.h (https://www.ibm.com/docs/es/i/7.5?topic=files-timeh)
 *   - Used to get the current time in order to generate signals with a real-like time variable instead of calculating/generating it
 * - stdbool.h (https://www.ibm.com/docs/es/i/7.5?topic=files-stdboolh)
 *   - Needed to support the use of logical and boolean operators.
 * - stdlib.h (https://www.ibm.com/docs/es/i/7.5?topic=files-stdlibh)
 *   - Standard C library. 
 * - stdio.h (https://www.ibm.com/docs/es/i/7.5?topic=files-stdioh)
 *   - Standard Input/Output library.
 * - hardware/gpio.h (https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio)
 *   - This library is mandatory to manipulate the General Purpose Input/Ouput ports available in the Raspberry Pi Pico board
 * - pico/stdlib.h (https://www.raspberrypi.com/documentation//pico-sdk/stdlib_8h_source.html)
 *   - Standard library associated to the Pico Board.
 * @section notes Notes
 * - This signal generator works implementing both polling and interrupt methodology. The first, for the keypad, the second for the pushbutton.
 * - Default values can be changed with testing purposes. 
 *
 * @section todo ToDo
 * - This code and its structure could be further optimized.
 *
 * @section author Authors
 * - Created by Santiago Giraldo Tabares & Ana María Velasco Montenegro on april, 2024
 *
 *
 * Copyright (c) No-license.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define DEBOUNCE_DELAY_US 10000 ///< Delay (uS). With debouncing purposes
#define AMPLITUDE_DEFAULT 100.0    ///< Amplitude value by default
#define AMPLITUDE_MIN 100.0       ///< Minimum amplitude (mV)
#define AMPLITUDE_MAX 2500.0      ///< Maximum amplitude (mV)
#define FREQUENCY_MIN 1           ///< Mininum frequency (Hz)
#define FREQUENCY_MAX 12000000    ///< Maximum frequency (Hz)
#define AMPLITUDE_MAX_DAC 255     ///< Maximum amplitude value to be converted by the 8-bit DAC (2^8 - 1)
#define VREF 3.3                  ///< Reference voltaje for 8-bit DAC
#define M_PI 3.14159265358979323846 ///< Value of PI

#define ROWS 4 ///< Amount of rows
#define COLS 4 ///< Amount of columns

char keys[ROWS][COLS] = {{'1','2','3','A'},{'4','5','6','B'},{'7','8','9','C'},{'*','0','#','D'}}; ///< 4x4 array, containing each of the characters present in the Keypad Layout. The order of the characters should match the order of the keypad itself.

uint rowPins[ROWS] = {18, 19, 20, 21}; ///< Rows pinout (GPIOs) in the RP2040
uint colPins[COLS] = {22, 26, 27, 28};   ///< Columns pinout (GPIOs) in the RP2040

const uint waveformButtonPin = 16;     ///< Pushbutton GPIO.

uint dacPins[8] = {0, 1, 2, 3, 4, 5, 6, 7}; ///< output GPIOs to be connected to the DAC. Ordered from LSB to MSB.

typedef enum {
    SINE, ///< Sinusoidal wave. Sin(2*pi*f*t). centered aroung its DC offset
    SQUARE, ///< Square wave, also called pulsed wave, symetric (50% duty cycle) centered aroung its DC offset
    SAWTOOTH, ///< Sawtooth wave. Rising time matches the period, while falling time goes to zero. centered aroung its DC offset
    TRIANGULAR ///< Triangular wave. Symetric (rising time equals 50% of period, falling time equals 50% of period). Centered aroung its DC offset
} Waveform; ///< Predefined signal waveforms.

Waveform currentWaveform = SINE; ///< Current waveform produced by the signal. In this line, set the default Signal waveform as Sinewave.

float amplitude = AMPLITUDE_DEFAULT; ///< Set current amplitude to DEFAULT value
float frequency = 10; ///< Set current frequency to  DEFAULT value
float dcOffset = (AMPLITUDE_MIN + AMPLITUDE_MAX) / 2.0; ///< Set current DC offset to DEFAULT value.

bool buttonState = false; ///< Set current state of the push-button as NOT-PRESSED.
uint64_t lastButtonPressTime = 0; ///< With debouncing purposes. Record the time when a button was pressed.


/**
 * The variable "currentWaveform" changes to the NEXT waveform declared in the enum. Once the last (4th) waveform is reached, the next would be the 1st (SINE). Also prints the current waveform.
 */
void changeWaveform() {
    currentWaveform = (Waveform)((currentWaveform + 1) % 4);
    switch (currentWaveform) {
        case SINE:
            printf("Forma de onda cambiada a: Seno\n");
            break;
        case SQUARE:
            printf("Forma de onda cambiada a: Cuadrada\n");
            break;
        case SAWTOOTH:
            printf("Forma de onda cambiada a: Diente de sierra\n");
            break;
        case TRIANGULAR:
            printf("Forma de onda cambiada a: Triangular\n");
            break;
    }
}


/**
 * Callback function to handle the push-button input. Once, the button is pressed, this routine is executed. Inside of it, the program call the ChangeWaveform function.
 */
void waveformButtonCallback(uint gpio, uint32_t events) {
    // Registrar el tiempo actual
    uint64_t currentTime = time_us_64();
    
    // Verificar si ha pasado el tiempo de debounce
    if (currentTime - lastButtonPressTime >= DEBOUNCE_DELAY_US) {
        // Cambiar la forma de onda cuando se detecte una interrupción en el botón
        if (gpio == waveformButtonPin && events == GPIO_IRQ_EDGE_RISE) {
            changeWaveform();
        }
        
        // Actualizar el tiempo de la última pulsación del botón
        lastButtonPressTime = currentTime;
    }
}


/**
 * Initializes each pin from the Raspberry pi pico needed to output the signal. In thise case, initializez 8 pins according to the 8 bits needed in the DAC. As long as 
 * the pins are in sequence, these could be initialized with a for loop. Also, sets up the Push-button with its own Pull-up resistor.
 */
void setup_gpio() {
    for (int i = 0; i < 8; i++) {
        gpio_init(dacPins[i]);
        gpio_set_dir(dacPins[i], GPIO_OUT);
    }
    gpio_init(waveformButtonPin);
    gpio_set_dir(waveformButtonPin, GPIO_IN);
    gpio_pull_up(waveformButtonPin);
    // Configurar interrupción en flanco de bajada para el botón
    gpio_set_irq_enabled_with_callback(waveformButtonPin, GPIO_IRQ_EDGE_FALL, true, &waveformButtonCallback);
}


/**
 * Writes the value in the DAC. As long as we are using an 8-bit DAC, resulting value (well, its binary representation) should be written through 8 GPIOs. In this case, using only shifting and bitwise and the write on the correponding pin, from LSB to MSB.
 *
 * @param value   This represent the voltage of the signal in the current time. scaled between 0 and 255.
 *
 */
void write_dac(uint8_t value) {
    for (int i = 0; i < 8; i++) {
        gpio_put(dacPins[i], (value >> i) & 1);
    }
}


/**
 * The standard Arduino setup function used for setup and configuration tasks. 115200 baud-rate is desired as we are working on Raspberry Pi Pico. Also prints the initial configuration of the system.
 */
void setup() {
    stdio_init_all();
    setup_gpio();
}


/**
 * Function to get input from the 4x4 matrix keypad. Also stablish each column as INPUT _PULLUP, and each row as OUTPUT. Checks in every ROW/COLUMN combination and if a row and a column match being LOW, the key corresponding to both column and row was pressed. 
 *  Then, save the key, debounces and wait for the key to be released.
 * @return  The key that was pressed.
 */
char getKeypadInput() {
    char key = '\0';
    for (uint col = 0; col < COLS; col++) {
        gpio_init(colPins[col]);
        gpio_set_dir(colPins[col], GPIO_IN);
        gpio_pull_up(colPins[col]);
    }

    for (uint row = 0; row < ROWS; row++) {
        gpio_init(rowPins[row]);
        gpio_set_dir(rowPins[row], GPIO_OUT);
        gpio_put(rowPins[row], 0);

        for (uint col = 0; col < COLS; col++) {
            if (!gpio_get(colPins[col])) {
                key = keys[row][col];
                sleep_ms(50); // Debounce delay
                while (!gpio_get(colPins[col])) {} // Wait for key release
            }
        }

        gpio_put(rowPins[row], 1);
        gpio_set_dir(rowPins[row], GPIO_IN);
    }

    return key;
}


/**
 * Only if the pressed key was an "A". Get input from the keypad and saves it in the amplitude variable. Converts it from string to float, then constrains it in the range of [AMPLITUDE_MIN, AMPLITUDE_MAX].
 */
void readAmplitude() {
    printf("Ingrese la amplitud (mV) [%.0f-%.0f]: ", AMPLITUDE_MIN, AMPLITUDE_MAX);
    char key = '\0';
    char amplitudeStr[16] = "";
    size_t amplitudeIndex = 0;
    while (key != 'D') {
        key = getKeypadInput();
        if (key >= '0' && key <= '9') {
            amplitudeStr[amplitudeIndex++] = key;
            printf("%c", key);
        }
        sleep_ms(200);
    }
    amplitudeStr[amplitudeIndex] = '\0';
    float amplitudeValue = strtof(amplitudeStr, NULL);
    amplitude = fminf(fmaxf(amplitudeValue, AMPLITUDE_MIN), AMPLITUDE_MAX);
    printf(" mV\n");
    printf("Amplitud establecida en: %.1f mV\n", amplitude);
}


/**
 * Only if the pressed key was an "B". Get input from the keypad and saves it in the frequency variable. Converts it from string to float, then constrains it in the range of [FREQUENCY_MIN, FREQUENCY_MAX].
 */
void readFrequency() {
    printf("Ingrese la frecuencia (Hz) [%d-%d]: ", FREQUENCY_MIN, FREQUENCY_MAX);
    char key = '\0';
    char frequencyStr[16] = "";
    size_t frequencyIndex = 0;
    while (key != 'D') {
        key = getKeypadInput();
        if (key >= '0' && key <= '9') {
            frequencyStr[frequencyIndex++] = key;
            printf("%c", key);
        }
        sleep_ms(200);
    }
    frequencyStr[frequencyIndex] = '\0';
    float frequencyValue = strtof(frequencyStr, NULL);
    frequency = fminf(fmaxf(frequencyValue, FREQUENCY_MIN), FREQUENCY_MAX);
    printf(" Hz\n");
    printf("Frecuencia establecida en: %.0f Hz\n", frequency);
}


/**
 * Only if the pressed key was an "C". Get input from the keypad and saves it in the dcOffset variable. Converts it from string to float, then constrains it in the range of [AMPLITUDE_MIN / 2.0, AMPLITUDE_MAX / 2.0].
 */
void readDCOffset() {
    printf("Ingrese el desplazamiento DC (mV) [%.1f-%.1f]: ", AMPLITUDE_MIN / 2.0, AMPLITUDE_MAX / 2.0);
    char key = '\0';
    char offsetStr[16] = "";
    size_t offsetIndex = 0;
    while (key != 'D') {
        key = getKeypadInput();
        if (key >= '0' && key <= '9') {
            offsetStr[offsetIndex++] = key;
            printf("%c", key);
        }
        sleep_ms(200);
    }
    offsetStr[offsetIndex] = '\0';
    float offsetValue = strtof(offsetStr, NULL);
    dcOffset = fminf(fmaxf(offsetValue, AMPLITUDE_MIN / 2.0), AMPLITUDE_MAX / 2.0);
    printf(" mV\n");
    printf("Desplazamiento DC establecido en: %.1f mV\n", dcOffset);
}


/**
 * Main loop function of the program. Executes the signal generation and polling functions corresponding to keypad handling, in cyclic way. 
 */
int main() {
    setup();
    while (true) {
    char key = getKeypadInput();
            if (key == 'A') {
                readAmplitude();
            } else if (key == 'B') {
                readFrequency();
            } else if (key == 'C') {
                readDCOffset();
            }

            if (!gpio_get(waveformButtonPin)) {
                sleep_ms(50); // Debounce delay
                if (!gpio_get(waveformButtonPin)) { // Check again after debounce delay
                    changeWaveform();
                    sleep_ms(500); // Debounce delay
                }
            }

            for (size_t i = 0; i < 256; i++) {
                uint64_t current_time = time_us_64();
                float value;
                switch (currentWaveform) {
                    case SINE:
                        value = (amplitude * 0.001 * (sinf(2.0f * M_PI * frequency * current_time / 1000000.0f))) + (dcOffset * 0.001);
                        break;
                    case SQUARE:
                        if (sinf(2.0f * M_PI * frequency * current_time / 1000000.0f) >= 0.0f) {
                            value = (amplitude * 0.001) + (dcOffset * 0.001); // Positive half of the square wave
                        } else {
                            value = -(amplitude * 0.001) + (dcOffset * 0.001); // Negative half of the square wave
                        }
                        break;
                    case SAWTOOTH:
                        value = (fmodf(frequency * current_time / 1000000.0f, 1.0f) * 2.0f - 1.0f) * (amplitude * 0.001) + (dcOffset * 0.001);
                        break;
                    case TRIANGULAR:
                        value = (2.0f * fabsf(2.0f * (frequency * current_time / 1000000.0f - floor(frequency * current_time / 1000000.0f + 0.5f))) - 1.0f) * (amplitude * 0.001 / 2.0) + (dcOffset * 0.001);
                        break;
                }
                float scaled_value = (value + 1.0f) * 127.5f;
                uint8_t dac_value = (uint8_t)fminf(fmaxf(scaled_value, 0.0f), 255.0f);
                write_dac(dac_value);

                sleep_us(1); // Sleep for a short duration for better accuracy
            }
    }
    return 0;
}
