#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/mssp1.h"


/* =========================================================
 * MCP9808
 * ========================================================= */

#define MCP9808_ADDRESS       0x1C
#define MCP9808_TEMP_REGISTER 0x05


/* =========================================================
 * VARIABLES PARA MPLAB X - WATCH
 * ========================================================= */

/*
 * Bytes recibidos desde el MCP9808.
 *
 * mcp9808_data[0] = byte alto
 * mcp9808_data[1] = byte bajo
 */
volatile uint8_t mcp9808_data[2] = {0, 0};


/*
 * Temperatura en grados enteros.
 *
 * Ejemplo:
 *
 * 23.81 °C -> 23
 */
volatile int16_t mcp9808_temperature = 0;


/*
 * Temperatura multiplicada por 16.
 *
 * Cada unidad representa 0.0625 °C.
 *
 * Ejemplo:
 *
 * 23.8125 °C
 *
 * 23.8125 x 16 = 381
 */
volatile int16_t mcp9808_temperature_x16 = 0;


/*
 * Temperatura en centésimas de grado.
 *
 * Ejemplo:
 *
 * 2381 = 23.81 °C
 *
 * 2500 = 25.00 °C
 */
volatile int16_t mcp9808_temperature_centi = 0;


/*
 * Estado de la comunicación.
 */
volatile bool mcp9808_ok = false;


/*
 * Indica que terminó la lectura.
 */
volatile bool mcp9808_finished = false;


/*
 * Error del bus I2C.
 */
volatile i2c_host_error_t mcp9808_error = I2C_ERROR_NONE;


/* =========================================================
 * ESPERAR A QUE TERMINE I2C
 * ========================================================= */

static bool MCP9808_WaitI2C(void)
{
    uint32_t timeout = 0;


    /*
     * El driver I2C generado por MCC trabaja
     * mediante interrupciones.
     *
     * Esperamos hasta que termine.
     */
    while (I2C1_IsBusy())
    {
        timeout++;


        /*
         * Protección contra bloqueo infinito.
         */
        if (timeout > 100000UL)
        {
            return false;
        }
    }


    return true;
}


/* =========================================================
 * LEER TEMPERATURA DEL MCP9808
 * ========================================================= */

static bool MCP9808_Read(void)
{
    uint8_t register_address;

    uint16_t raw_temperature;

    int16_t temperature_x16;

    int16_t temperature_centi;


    /*
     * Registro de temperatura del MCP9808.
     */
    register_address = MCP9808_TEMP_REGISTER;


    /*
     * Estado inicial de la lectura.
     */
    mcp9808_finished = false;

    mcp9808_ok = false;

    mcp9808_error = I2C_ERROR_NONE;


    /*
     * Limpiar datos anteriores.
     */
    mcp9808_data[0] = 0;

    mcp9808_data[1] = 0;


    /*
     * -----------------------------------------------------
     * I2C:
     *
     * Escribir:
     *     0x05
     *
     * Después leer:
     *     2 bytes
     * -----------------------------------------------------
     */
    if (!I2C1_WriteRead(
            MCP9808_ADDRESS,
            &register_address,
            1,
            (uint8_t *)mcp9808_data,
            2))
    {
        /*
         * La transferencia no pudo iniciarse.
         */
        mcp9808_error = I2C1_ErrorGet();

        mcp9808_finished = true;

        return false;
    }


    /*
     * Esperar a que termine la transferencia.
     */
    if (!MCP9808_WaitI2C())
    {
        /*
         * Timeout.
         */
        mcp9808_error = I2C_ERROR_BUS_COLLISION;

        mcp9808_finished = true;

        return false;
    }


    /*
     * Obtener el estado final del bus.
     */
    mcp9808_error = I2C1_ErrorGet();


    /*
     * Si hubo error, terminar.
     */
    if (mcp9808_error != I2C_ERROR_NONE)
    {
        mcp9808_finished = true;

        return false;
    }


    /*
     * -----------------------------------------------------
     * CONSTRUIR LOS 16 BITS
     * -----------------------------------------------------
     *
     * Byte alto:
     *     mcp9808_data[0]
     *
     * Byte bajo:
     *     mcp9808_data[1]
     */
    raw_temperature =
        ((uint16_t)mcp9808_data[0] << 8)
        |
        (uint16_t)mcp9808_data[1];


    /*
     * -----------------------------------------------------
     * CONVERSIÓN DE TEMPERATURA
     * -----------------------------------------------------
     *
     * El MCP9808 utiliza:
     *
     * bit 12 = signo
     *
     * bits 11:0 = temperatura
     *
     * resolución = 0.0625 °C
     *
     * es decir:
     *
     * 1 / 16 °C
     * -----------------------------------------------------
     */


    /*
     * -----------------------------------------------------
     * TEMPERATURA NEGATIVA
     * -----------------------------------------------------
     */
    if ((raw_temperature & 0x1000U) != 0U)
    {
        /*
         * Conservar solamente los 12 bits
         * correspondientes a temperatura.
         */
        raw_temperature &= 0x0FFFU;


        /*
         * Convertir a formato x16.
         *
         * Ejemplo conceptual:
         *
         * -1 °C = -16
         *
         * -0.0625 °C = -1
         */
        temperature_x16 =
            -(int16_t)(4096U - raw_temperature);
    }


    /*
     * -----------------------------------------------------
     * TEMPERATURA POSITIVA
     * -----------------------------------------------------
     */
    else
    {
        /*
         * Conservar solamente los 12 bits
         * correspondientes a temperatura.
         */
        raw_temperature &= 0x0FFFU;


        /*
         * El valor recibido ya está expresado
         * en unidades de 1/16 °C.
         */
        temperature_x16 =
            (int16_t)raw_temperature;
    }


    /*
     * -----------------------------------------------------
     * GUARDAR TEMPERATURA x16
     * -----------------------------------------------------
     *
     * Ejemplo:
     *
     * 23.8125 °C
     *
     * x16 = 381
     */
    mcp9808_temperature_x16 =
        temperature_x16;


    /*
     * -----------------------------------------------------
     * TEMPERATURA EN GRADOS ENTEROS
     * -----------------------------------------------------
     */
    if (temperature_x16 >= 0)
    {
        mcp9808_temperature =
            temperature_x16 / 16;
    }
    else
    {
        mcp9808_temperature =
            -((-temperature_x16) / 16);
    }


    /*
     * -----------------------------------------------------
     * TEMPERATURA EN CENTÉSIMAS
     * -----------------------------------------------------
     *
     * Sabemos que:
     *
     * 1 unidad x16 = 0.0625 °C
     *
     * Entonces:
     *
     * temperatura_centi =
     *
     * temperatura_x16 x 100 / 16
     *
     * Como:
     *
     * 100 / 16 = 6.25
     *
     * podemos utilizar:
     *
     * x16 x 25 / 4
     *
     *
     * Ejemplo:
     *
     * 381 x 25 / 4
     *
     * = 9525 / 4
     *
     * = 2381
     *
     * Por tanto:
     *
     * 2381 = 23.81 °C
     * -----------------------------------------------------
     */

    if (temperature_x16 >= 0)
    {
        temperature_centi =
            (int16_t)(
                ((int32_t)temperature_x16 * 25L) / 4L
            );
    }
    else
    {
        temperature_centi =
            -(int16_t)(
                ((int32_t)(-temperature_x16) * 25L) / 4L
            );
    }


    /*
     * Guardar resultado para Watch.
     */
    mcp9808_temperature_centi =
        temperature_centi;


    /*
     * La lectura terminó correctamente.
     */
    mcp9808_ok = true;

    mcp9808_finished = true;


    return true;
}


/* =========================================================
 * RETARDO SIMPLE
 * ========================================================= */

static void MCP9808_Delay(void)
{
    volatile uint32_t delay;


    for (delay = 0;
         delay < 50000UL;
         delay++)
    {
        ;
    }
}


/* =========================================================
 * MAIN
 * ========================================================= */

void main(void)
{
    /*
     * -----------------------------------------------------
     * INICIALIZACIÓN DEL SISTEMA
     * -----------------------------------------------------
     *
     * Código generado por MCC.
     */
    SYSTEM_Initialize();


    /*
     * -----------------------------------------------------
     * INICIALIZACIÓN DEL I2C
     * -----------------------------------------------------
     */
    I2C1_Initialize();


    /*
     * -----------------------------------------------------
     * INICIALIZAR VARIABLES
     * -----------------------------------------------------
     */

    mcp9808_data[0] = 0;

    mcp9808_data[1] = 0;


    mcp9808_temperature = 0;

    mcp9808_temperature_x16 = 0;

    mcp9808_temperature_centi = 0;


    mcp9808_ok = false;

    mcp9808_finished = false;


    mcp9808_error = I2C_ERROR_NONE;


    /*
     * -----------------------------------------------------
     * HABILITAR INTERRUPCIONES
     * -----------------------------------------------------
     *
     * Para el PIC16F13145 el registro es:
     *
     * INTCONbits
     *
     * NO:
     *
     * INTCON0bits
     */
    INTCONbits.PEIE = 1;

    INTCONbits.GIE = 1;


    /*
     * -----------------------------------------------------
     * BUCLE PRINCIPAL
     * -----------------------------------------------------
     */
    while (1)
    {
        /*
         * Leer temperatura del MCP9808.
         */
        MCP9808_Read();


        /*
         * Pequeña espera antes de la siguiente lectura.
         */
        MCP9808_Delay();
    }
}