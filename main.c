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
 * VARIABLES PARA MPLAB X WATCH
 * ========================================================= */

/* Bytes recibidos del MCP9808 */
uint8_t mcp9808_data[2] = {0, 0};

/*
 * Temperatura en grados enteros.
 *
 * Ejemplo:
 * 24 = 24 °C
 */
int16_t mcp9808_temperature = 0;


/*
 * Temperatura multiplicada por 16.
 *
 * Ejemplo:
 *
 * 24.000 °C -> 384
 * 24.0625 °C -> 385
 * 24.125 °C -> 386
 *
 * Esto permite observar la parte fraccionaria.
 */
int16_t mcp9808_temperature_x16 = 0;


/* Estado de comunicación */
bool mcp9808_ok = false;
bool mcp9808_finished = false;


/* Error I2C */
i2c_host_error_t mcp9808_error = I2C_ERROR_NONE;


/* =========================================================
 * MCP9808 - LEER TEMPERATURA
 * ========================================================= */

static bool MCP9808_ReadTemperature(void)
{
    uint8_t register_address;
    uint16_t raw_temperature;
    uint32_t timeout;


    /*
     * Registro de temperatura del MCP9808.
     */
    register_address = MCP9808_TEMP_REGISTER;


    /*
     * Estados iniciales.
     */
    mcp9808_finished = false;
    mcp9808_ok = false;
    mcp9808_error = I2C_ERROR_NONE;


    /*
     * Limpiar buffer.
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
     * Luego leer:
     *     2 bytes
     * -----------------------------------------------------
     */
    if (!I2C1_WriteRead(
            MCP9808_ADDRESS,
            &register_address,
            1,
            mcp9808_data,
            2))
    {
        /*
         * La transferencia no pudo comenzar.
         */
        mcp9808_error = I2C1_ErrorGet();
        mcp9808_finished = true;

        return false;
    }


    /*
     * -----------------------------------------------------
     * Esperar a que termine el I2C.
     * -----------------------------------------------------
     */
    timeout = 0;

    while (I2C1_IsBusy())
    {
        timeout++;

        /*
         * Evitar bloqueo infinito.
         */
        if (timeout > 100000UL)
        {
            mcp9808_error = I2C_ERROR_BUS_COLLISION;
            mcp9808_finished = true;

            return false;
        }
    }


    /*
     * Obtener resultado de la transferencia.
     */
    mcp9808_error = I2C1_ErrorGet();


    if (mcp9808_error != I2C_ERROR_NONE)
    {
        mcp9808_finished = true;

        return false;
    }


    /*
     * -----------------------------------------------------
     * Combinar los dos bytes.
     *
     * Ejemplo de tu captura:
     *
     * data[0] = C1
     * data[1] = 82
     *
     * raw = C182
     * -----------------------------------------------------
     */
    raw_temperature =
        ((uint16_t)mcp9808_data[0] << 8) |
        mcp9808_data[1];


    /*
     * -----------------------------------------------------
     * MCP9808:
     *
     * Resolución = 0.0625 °C
     *
     * Los bits 12..0 contienen la temperatura.
     * -----------------------------------------------------
     */
    raw_temperature &= 0x1FFF;


    /*
     * -----------------------------------------------------
     * Temperatura negativa
     * -----------------------------------------------------
     */
    if ((raw_temperature & 0x1000U) != 0U)
    {
        /*
         * Quitar bit de signo.
         */
        raw_temperature &= 0x0FFFU;


        /*
         * Guardar temperatura en unidades de 1/16 °C.
         *
         * Ejemplo:
         *
         * -10.125 °C
         *
         * -> -162
         */
        mcp9808_temperature_x16 =
            -(int16_t)(4096U - raw_temperature);
    }
    else
    {
        /*
         * Temperatura positiva.
         *
         * El valor queda directamente
         * expresado en unidades de 1/16 °C.
         */
        mcp9808_temperature_x16 =
            (int16_t)raw_temperature;
    }


    /*
     * -----------------------------------------------------
     * Temperatura entera.
     *
     * 386 / 16 = 24
     * -----------------------------------------------------
     */
    if (mcp9808_temperature_x16 >= 0)
    {
        mcp9808_temperature =
            mcp9808_temperature_x16 / 16;
    }
    else
    {
        mcp9808_temperature =
            mcp9808_temperature_x16 / 16;
    }


    /*
     * Comunicación correcta.
     */
    mcp9808_ok = true;
    mcp9808_finished = true;


    return true;
}


/* =========================================================
 * MAIN
 * ========================================================= */

void main(void)
{
    /*
     * -----------------------------------------------------
     * Inicialización del sistema generada por MCC.
     * -----------------------------------------------------
     */
    SYSTEM_Initialize();


    /*
     * -----------------------------------------------------
     * Inicialización del I2C.
     * -----------------------------------------------------
     */
    I2C1_Initialize();


    /*
     * -----------------------------------------------------
     * Inicializar variables.
     * -----------------------------------------------------
     */
    mcp9808_data[0] = 0;
    mcp9808_data[1] = 0;

    mcp9808_temperature = 0;

    mcp9808_temperature_x16 = 0;

    mcp9808_ok = false;
    mcp9808_finished = false;

    mcp9808_error = I2C_ERROR_NONE;


    /*
     * -----------------------------------------------------
     * Habilitar interrupciones.
     *
     * En el PIC16F13145 el registro correcto es INTCONbits.
     * -----------------------------------------------------
     */
    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;


    /*
     * -----------------------------------------------------
     * Bucle principal.
     * -----------------------------------------------------
     */
    while (1)
    {
        /*
         * Leer MCP9808.
         */
        MCP9808_ReadTemperature();


        /*
         * Pequeña espera antes de la siguiente lectura.
         */
        for (volatile uint32_t delay = 0;
             delay < 50000UL;
             delay++)
        {
            ;
        }
    }
}