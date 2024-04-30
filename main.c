/**
 * @file main.c
 * 
 * @mainpage Generador de Señales Digital con Interrupciones y Teclado Matricial
 * 
 * @section description Descripción
 * Este programa convierte la Raspberry Pi Pico en un generador de señales digital programable
 * usando interrupciones para cambiar entre diferentes formas de onda y para capturar los inputs del
 * teclado matricial, ajustando los parámetros de amplitud, frecuencia y desplazamiento DC.
 * 
 * @section circuit Circuito
 * - Botón conectado a GP16 para cambio de forma de onda.
 * - Filas del teclado matricial conectadas a GP18, GP19, GP20, GP21
 * - Columnas del teclado matricial conectadas a GP22, GP26, GP27, GP28
 * 
 * @section libraries Bibliotecas
 * - pico/stdlib.h
 * - hardware/gpio.h
 * - hardware/irq.h
 * 
 * @section notes Notas
 * - Este programa utiliza interrupciones para todas las entradas de usuario para mejorar la eficiencia.
 * 
 * @section todo Por hacer
 * - Añadir funcionalidades adicionales y optimizar el manejo de errores.
 * 
 * @section author Autores
 * - Santiago Giraldo Tabares & Ana María Velasco Montenegro, abril de 2024
 * 
 * @section copyright Copyright
 * - Sin licencia, uso libre.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define DEBOUNCE_MS 200 ///< Retraso (uS). Con fines de eliminación de rebotes
#define AMPLITUDE_MIN 100.0 ///< Amplitud mínima (mV)
#define AMPLITUDE_MAX 2500.0 ///< Amplitud máxima (mV)
#define FREQUENCY_MIN 1 ///< Frecuencia mínima (Hz)
#define FREQUENCY_MAX 12000000 ///< Frecuencia máxima (Hz)
#define DAC_MAX_VALUE 255 ///< Valor máximo de amplitud a convertir por el DAC de 8 bits (2^8 - 1)
#define VREF 3.3 ///< Voltaje de referencia para el DAC de 8 bits
#define M_PI 3.141592 ///< Valor de PI
#define ROWS 4 ///< Cantidad de filas
#define COLS 4 ///< Cantidad de columnas
#define WAVEFORM_BUTTON_PIN 16
#define DAC_PIN 0

uint rowPins[ROWS] = {18, 19, 20, 21}; ///< Disposición de pines de las filas (GPIOs) en el RP2040
uint colPins[COLS] = {22, 26, 27, 28}; ///< Disposición de pines de las columnas (GPIOs) en el RP2040
char keys[ROWS][COLS] = {{'1','2','3','A'},{'4','5','6','B'},{'7','8','9','C'},{'*','0','#','D'}}; ///< Matriz 4x4, que contiene cada uno de los caracteres presentes en el diseño del teclado. El orden de los caracteres debe coincidir con el orden del teclado en sí.

typedef enum {
    SINE, ///< Onda sinusoidal. Sin(2*pi*f*t). centrada alrededor de su desplazamiento DC
    SQUARE, ///< Onda cuadrada, también llamada onda pulsada, simétrica (ciclo de trabajo del 50%) centrada alrededor de su desplazamiento DC
    SAWTOOTH, ///< Onda diente de sierra. El tiempo de subida coincide con el período, mientras que el tiempo de caída va a cero. centrada alrededor de su desplazamiento DC
    TRIANGULAR ///< Onda triangular. Simétrica (tiempo de subida igual al 50% del período, tiempo de caída igual al 50% del período). Centrada alrededor de su desplazamiento DC
} Waveform;

volatile Waveform current_waveform = SINE; ///< Forma de onda actual producida por la señal. En esta línea, establece la forma de onda de señal predeterminada como onda sinusoidal.
volatile float amplitude = 1000.0; ///< Establece la amplitud actual en el valor por DEFECTO
volatile float frequency = 10.0; ///< Establece la frecuencia actual en el valor por DEFECTO
volatile float dc_offset = 500.0; ///< Establece el desplazamiento DC actual en el valor por DEFECTO.

char paramType = 0;
char inputBuffer[20];
int inputIndex = 0;

// Función para manejar las interrupciones de los GPIO
void gpio_callback(uint gpio, uint32_t events);

// Función para inicializar los GPIO
void setup_gpio();

// Función para manejar la entrada del teclado
void handle_input(char key);

// Función para generar la forma de onda
void generate_waveform();

/**
 * Función principal del programa. Trabajando solo con llamadas de subrutina de interrupción.
 */
int main() {
    stdio_init_all();
    setup_gpio();
    printf("Signal Generator Started.\n");

    while (true) {
        generate_waveform();
    }

    return 0;
}

/**
 * Inicializa cada pin de la Raspberry Pi Pico necesario para producir la señal. En este caso, inicializa 8 pines de acuerdo con los 8 bits necesarios en el DAC. Mientras
 * los pines estén en secuencia, estos podrían inicializarse con un bucle for. Además, configura el botón de pulsación con su propio resistor de pull-up.
 */
void setup_gpio() {
    // Configuración para el DAC y el botón de forma de onda
    gpio_init(DAC_PIN);
    gpio_set_dir(DAC_PIN, GPIO_OUT);

    gpio_init(WAVEFORM_BUTTON_PIN);
    gpio_set_dir(WAVEFORM_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(WAVEFORM_BUTTON_PIN);
    gpio_set_irq_enabled_with_callback(WAVEFORM_BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    // Configuración para el teclado
    for (int i = 0; i < ROWS; i++) {
        gpio_init(rowPins[i]);
        gpio_set_dir(rowPins[i], GPIO_OUT);
        gpio_put(rowPins[i], 1);
    }

    for (int i = 0; i < COLS; i++) {
        gpio_init(colPins[i]);
        gpio_set_dir(colPins[i], GPIO_IN);
        gpio_pull_up(colPins[i]);
        gpio_set_irq_enabled_with_callback(colPins[i], GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    }
}

/**
 * Esta función genera una forma de onda basada en la ecuación correspondiente a la forma de onda deseada.
 */
void generate_waveform() {
    static uint64_t last_time = 0;
    uint64_t current_time = time_us_64();
    float value = 0;

    // Calcular el paso de tiempo
    float time_step = (current_time - last_time) / 1000000.0f;
    last_time = current_time;

    switch (current_waveform) {
        case SINE:
            value = amplitude * (sinf(2 * M_PI * frequency * time_step) / 2 + 0.5) + dc_offset;
            break;
        case SQUARE:
            value = amplitude * (sinf(2 * M_PI * frequency * time_step) > 0 ? 1 : -1) / 2 + 0.5 + dc_offset;
            break;
        case SAWTOOTH:
            value = amplitude * (2 * fmod(frequency * time_step, 1) - 1) / 2 + 0.5 + dc_offset;
            break;
        case TRIANGULAR:
            value = amplitude * (1.0 - fabs(fmod(frequency * time_step * 4, 4) - 2)) + dc_offset;
            break;
    }

    // Convertir a valor DAC
    uint dac_value = (uint)((value / VREF) * DAC_MAX_VALUE);
    gpio_put(DAC_PIN, dac_value);  // Suponiendo que DAC_PIN está conectado a un DAC de escalera R2R simple o similar
}

/**
 * Función para manejar las interrupciones de los GPIO. Debe manejar tanto el botón como el teclado.
 */
void gpio_callback(uint gpio, uint32_t events) {
    static uint64_t last_interrupt_time = 0;
    uint64_t current_time = to_ms_since_boot(get_absolute_time());

    if (current_time - last_interrupt_time > DEBOUNCE_MS) {
        last_interrupt_time = current_time;

        if (gpio == WAVEFORM_BUTTON_PIN) {
            current_waveform = (Waveform)((current_waveform + 1) % 4);
            printf("Forma de onda cambiada a %d\n", current_waveform);
        } else {
            for (int row = 0; row < ROWS; ++row) {
                gpio_put(rowPins[row], 0);  // Activar la fila
                for (int col = 0; col < COLS; ++col) {
                    if (!gpio_get(colPins[col])) {
                        char key = keys[row][col];
                        printf("Tecla presionada: %c\n", key);
                        handle_input(key);
                    }
                }
                gpio_put(rowPins[row], 1);  // Desactivar la fila
            }
        }
    }
}

/**
 * Obtener la entrada y convertirla de cadena a valor flotante, para amplitud, frecuencia y desplazamiento.
 */
void handle_input(char key) {
    if (key == 'D') {
        if (paramType == 'A') {
            amplitude = strtof(inputBuffer, NULL);
            printf("Amplitud establecida en: %.2f mV\n", amplitude);
        } else if (paramType == 'B') {
            frequency = strtof(inputBuffer,NULL);
            printf("Frecuencia establecida en: %.2f Hz\n", frequency);
        } else if (paramType == 'C') {
            dc_offset = strtof(inputBuffer,NULL);
            printf("Desplazamiento DC establecido en: %.2f mV\n", dc_offset);
        }
        inputIndex = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
    } else {
        if (key == 'A' || key == 'B' || key == 'C') {
            paramType = key;
        } else if (isdigit(key)) {
            if (inputIndex < sizeof(inputBuffer) - 1) {
                inputBuffer[inputIndex++] = key;
            }
        }
    }
}
