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
 * VARIABLES
 * ========================================================= */

volatile uint8_t mcp9808_data[2] = {0, 0};

volatile int16_t mcp9808_temperature = 0;

volatile bool mcp9808_ok = false;
volatile bool mcp9808_finished = false;

volatile i2c_host_error_t mcp9808_error = I2C_ERROR_NONE;


/* =========================================================
 * LEER TEMPERATURA
 * ========================================================= */

static bool MCP9808_Read(void)
{
    uint8_t register_address;
    uint32_t timeout;
    uint16_t raw_temperature;

    register_address = MCP9808_TEMP_REGISTER;

    mcp9808_finished = false;
    mcp9808_ok = false;
    mcp9808_error = I2C_ERROR_NONE;

    mcp9808_data[0] = 0;
    mcp9808_data[1] = 0;


    /* -----------------------------------------------------
     * Escribir registro 0x05 y leer 2 bytes
     * ----------------------------------------------------- */

    if (!I2C1_WriteRead(
            MCP9808_ADDRESS,
            &register_address,
            1,
            (uint8_t *)mcp9808_data,
            2))
    {
        mcp9808_error = I2C1_ErrorGet();
        mcp9808_finished = true;

        return false;
    }


    /* -----------------------------------------------------
     * Esperar finalización del I2C
     * ----------------------------------------------------- */

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


    /* -----------------------------------------------------
     * Comprobar error I2C
     * ----------------------------------------------------- */

    mcp9808_error = I2C1_ErrorGet();

    if (mcp9808_error != I2C_ERROR_NONE)
    {
        mcp9808_finished = true;

        return false;
    }


    /* -----------------------------------------------------
     * Convertir temperatura MCP9808
     * ----------------------------------------------------- */

    raw_temperature =
        ((uint16_t)mcp9808_data[0] << 8) |
        mcp9808_data[1];


    /* Temperatura negativa */
    if ((raw_temperature & 0x1000U) != 0U)
    {
        raw_temperature &= 0x0FFFU;

        mcp9808_temperature =
            -(int16_t)((4096U - raw_temperature) / 16U);
    }
    else
    {
        /* Temperatura positiva */
        raw_temperature &= 0x0FFFU;

        mcp9808_temperature =
            (int16_t)(raw_temperature / 16U);
    }


    /* -----------------------------------------------------
     * Lectura correcta
     * ----------------------------------------------------- */

    mcp9808_ok = true;
    mcp9808_finished = true;

    return true;
}


/* =========================================================
 * MAIN
 * ========================================================= */

void main(void)
{
    /* Inicialización MCC */
    SYSTEM_Initialize();

    /* Inicialización I2C */
    I2C1_Initialize();


    /* Inicializar variables */
    mcp9808_data[0] = 0;
    mcp9808_data[1] = 0;

    mcp9808_temperature = 0;

    mcp9808_ok = false;
    mcp9808_finished = false;

    mcp9808_error = I2C_ERROR_NONE;


    /*
     * IMPORTANTE:
     *
     * Para el PIC16F13145 se utilizan INTCONbits,
     * no INTCON0bits.
     */

    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;


    /* =====================================================
     * BUCLE PRINCIPAL
     * ===================================================== */

    while (1)
    {
        MCP9808_Read();


        /*
         * Retardo entre mediciones
         */
        for (volatile uint32_t delay = 0;
             delay < 50000UL;
             delay++)
        {
            ;
        }
    }
}