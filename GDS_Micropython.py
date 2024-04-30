## @package main
#  Este script convierte la Raspberry Pi Pico en un generador de señales programable.
#  El programa permite la entrada mediante un botón o un teclado matricial de 4x4 y genera
#  una salida de 8 bits que debe alimentarse a un DAC de 8 bits para obtener una señal analógica.
#  Utiliza tanto métodos de interrupción como de sondeo para manejar las entradas del botón y del teclado,
#  respectivamente. El botón se usa para cambiar entre diferentes formas de onda (seno, cuadrada, diente de sierra y triangular),
#  mientras que el teclado matricial permite al usuario introducir los valores deseados de amplitud, frecuencia y desplazamiento de CC.
#
#  @section circuit Circuito
#  - Botón conectado a GP16.
#  - Filas del teclado conectadas a (GP18, GP19, GP20, GP21), las columnas a (GP22, GP26, GP27, GP28).
#    La configuración de los pines puede modificarse siempre que la inicialización no requiera pines consecutivos.
#
#  @section libraries Bibliotecas
#  - machine: Para manejar pines y PWM.
#  - utime: Para manejar tiempos y retardos.
#  - math: Utilizada para realizar cálculos matemáticos necesarios para generar las formas de onda.
#
#  @section notes Notas
#  - Este generador de señales implementa metodologías tanto de sondeo como de interrupción.
#    El sondeo para el teclado y la interrupción para el botón.
#  - Los valores predeterminados pueden modificarse con fines de prueba.
#
#  @section todo Por hacer
#  - Este código y su estructura podrían optimizarse aún más para mejorar la eficiencia y la respuesta.
#
#  @section author Autores
#  - Creado por Santiago Giraldo Tabares & Ana María Velasco Montenegro en abril de 2024.
#
#  @section copyright Copyright
#  - Copyright (C) Sin licencia.

from machine import Pin, PWM
import utime
import math

## Configuración de pines para las columnas y las filas del teclado matricial
cols_pins = [2, 3, 4, 5]
rows_pins = [6, 7, 8, 9]

## Configurar las columnas como salidas y ponerlas en estado alto inicialmente
cols = [Pin(pin_num, Pin.OUT) for pin_num in cols_pins]
for col in cols:
    col.value(1)

## Configurar las filas como entradas con resistencias de pull-up
rows = [Pin(pin_num, Pin.IN, Pin.PULL_UP) for pin_num in rows_pins]

## Mapa de teclas a su posición en la matriz
key_map = [
    ['1', '2', '3', 'A'],
    ['4', '5', '6', 'B'],
    ['7', '8', '9', 'C'],
    ['*', '0', '#', 'D']
]

## Configuración inicial para la señal PWM
pwm = PWM(Pin(15))
pwm.freq(10)  # Frecuencia de 10 Hz inicial

## Valores iniciales
amplitude = 1000  # Amplitud inicial de 1000 mV
dc_offset = 500   # Nivel DC inicial de 500 mV
frequency = 10    # Frecuencia inicial de 10 Hz
wave_forms = ['sine', 'triangle', 'sawtooth', 'square']
current_waveform_index = 0  # Índice inicial para la forma de onda

## Botón para cambiar la forma de onda
button = Pin(16, Pin.IN, Pin.PULL_DOWN)

## Manejador de interrupción para el botón de cambio de forma de onda
#  @param pin Pin que activó la interrupción.
def button_press_handler(pin):
    global current_waveform_index, wave_forms
    current_waveform_index = (current_waveform_index + 1) % len(wave_forms)
    print("Cambio de forma de onda a:", wave_forms[current_waveform_index])

button.irq(trigger=Pin.IRQ_RISING, handler=button_press_handler)

## Lee el teclado matricial y devuelve la tecla presionada
def read_keypad():
    for col_num, col in enumerate(cols):
        col.value(0)
        for row_num, row in enumerate(rows):
            if not row.value():
                utime.sleep_ms(20)
                if not row.value():
                    col.value(1)
                    return key_map[row_num][col_num]
        col.value(1)
    return None

## Solicita entrada numérica del usuario
#  @param prompt Texto a mostrar al usuario.
#  @param min_val Valor mínimo aceptable.
#  @param max_val Valor máximo aceptable.
def get_numeric_input(prompt, min_val, max_val):
    print(prompt)
    value = ''
    while True:
        key = read_keypad()
        if key and key.isdigit():
            value += key
            print(value)  # Echo the digit
        elif key == 'D' and value:  # Complete on 'D' press
            numeric_value = int(value)
            if numeric_value < min_val or numeric_value > max_val:
                print("Valor fuera de rango. Intente nuevamente.")
                value = ''  # Reset value if out of range
            else:
                return numeric_value
        utime.sleep(0.1)

## Configura los parámetros iniciales utilizando el teclado
def setup_parameters():
    global amplitude, dc_offset, frequency
    amplitude = get_numeric_input("Ingrese la amplitud (100-2500 mV): ", 100, 2500)
    dc_offset = get_numeric_input("Ingrese el desplazamiento de CC (50-1250 mV): ", 50, 1250)
    frequency = get_numeric_input("Ingrese la frecuencia (1-12000000 Hz): ", 1, 12000000)

## Genera la forma de onda basada en los parámetros configurados
def generate_wave():
    global pwm, amplitude, dc_offset, frequency, current_waveform_index, wave_forms
    safe_frequency = max(frequency, 1)
    step = 0
    while step < 360:
        wave_form = wave_forms[current_waveform_index]
        if wave_form == 'sine':
            duty = amplitude * (math.sin(math.radians(step)) / 2 + 0.5) + dc_offset
        elif wave_form == 'triangle':
            duty = (2 * amplitude / math.pi) * math.asin(math.sin(math.radians(step))) + dc_offset
        elif wave_form == 'sawtooth':
            duty = ((-2 * amplitude / math.pi) * math.atan(1/math.tan(math.radians(step / 2)))) + dc_offset
        elif wave_form == 'square':
            duty = amplitude if step < 180 else 0
            duty += dc_offset
        pwm.duty_u16(int((duty / 3300) * 65535))
        step += 360 / safe_frequency
        utime.sleep_us(int(1000000 / safe_frequency / 360))

## Función principal que configura los parámetros y ejecuta el bucle de generación de señales
def main():
    setup_parameters()
    last_time_printed = utime.time()
    while True:
        generate_wave()
        current_time = utime.time()
        if current_time - last_time_printed >= 1:
            print("Amplitud: {} mV, Desplazamiento DC: {} mV, Frecuencia: {} Hz, Forma de onda: {}".format(
                amplitude, dc_offset, frequency, wave_forms[current_waveform_index]))
            last_time_printed = current_time
        utime.sleep(0.1)

if __name__ == '__main__':
    main()
