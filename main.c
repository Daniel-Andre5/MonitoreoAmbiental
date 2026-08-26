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

/* Bytes recibidos del MCP9808 */
volatile uint8_t mcp9808_data[2] = {0, 0};

/* Temperatura calculada */
volatile int16_t mcp9808_temperature = 0;

/* Estado de la comunicación */
volatile bool mcp9808_ok = false;
volatile bool mcp9808_finished = false;

/* Error del bus I2C */
volatile i2c_host_error_t mcp9808_error = I2C_ERROR_NONE;


/* =========================================================
 * LEER TEMPERATURA
 * ========================================================= */

static bool MCP9808_Read(void)
{
    uint8_t register_address;
    uint32_t timeout;

    register_address = MCP9808_TEMP_REGISTER;

    mcp9808_finished = false;
    mcp9808_ok = false;
    mcp9808_error = I2C_ERROR_NONE;

    mcp9808_data[0] = 0;
    mcp9808_data[1] = 0;

    /*
     * Escribe el registro 0x05 y después
     * lee dos bytes del MCP9808.
     */
    if (!I2C1_WriteRead(
            MCP9808_ADDRESS,
            &register_address,
            1,
            mcp9808_data,
            2))
    {
        mcp9808_error = I2C1_ErrorGet();
        mcp9808_finished = true;

        return false;
    }


    /*
     * Esperar a que termine la transferencia.
     *
     * Se utiliza timeout para evitar que el programa
     * quede bloqueado indefinidamente.
     */
    timeout = 0;

    while (I2C1_IsBusy())
    {
        timeout++;

        if (timeout > 100000UL)
        {
            mcp9808_error = I2C_ERROR_BUS_COLLISION;
            mcp9808_finished = true;

            return false;
        }
    }


    /*
     * Obtener resultado de la comunicación.
     */
    mcp9808_error = I2C1_ErrorGet();

    if (mcp9808_error != I2C_ERROR_NONE)
    {
        mcp9808_finished = true;

        return false;
    }


    /*
     * Convertir los datos recibidos.
     *
     * El MCP9808 utiliza una resolución de 0.0625 °C.
     */
    {
        uint16_t raw_temperature;

        raw_temperature =
            ((uint16_t)mcp9808_data[0] << 8) |
            mcp9808_data[1];


        /*
         * Temperatura negativa.
         */
        if ((raw_temperature & 0x1000U) != 0U)
        {
            raw_temperature &= 0x0FFFU;

            /*
             * Para esta variable guardamos
             * la temperatura en grados enteros.
             */
            mcp9808_temperature =
                -(int16_t)((4096U - raw_temperature) / 16U);
        }
        else
        {
            /*
             * Temperatura positiva.
             */
            raw_temperature &= 0x0FFFU;

            mcp9808_temperature =
                (int16_t)(raw_temperature / 16U);
        }
    }


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
     * Inicialización generada por MCC.
     */
    SYSTEM_Initialize();


    /*
     * Inicialización del I2C MSSP1.
     */
    I2C1_Initialize();


    /*
     * Inicializar variables.
     */
    mcp9808_data[0] = 0;
    mcp9808_data[1] = 0;

    mcp9808_temperature = 0;

    mcp9808_ok = false;
    mcp9808_finished = false;

    mcp9808_error = I2C_ERROR_NONE;


    /*
     * Habilitar interrupciones.
     *
     * El driver I2C generado por MCC utiliza
     * interrupciones para realizar la transferencia.
     */
    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;


    /*
     * Bucle principal.
     */
    while (1)
    {
        /*
         * Leer temperatura.
         */
        MCP9808_Read();


        /*
         * Espera entre lecturas.
         */
        for (volatile uint32_t delay = 0;
             delay < 50000UL;
             delay++)
        {
            ;
        }
    }
}