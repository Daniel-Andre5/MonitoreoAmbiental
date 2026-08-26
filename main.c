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
 * Bytes recibidos del MCP9808.
 *
 * NO se declara volatile para evitar el warning al pasar
 * el buffer a I2C1_WriteRead(), cuya función espera
 * uint8_t *.
 */
uint8_t mcp9808_data[2] = {0, 0};


/*
 * Temperatura en grados Celsius enteros.
 *
 * Ejemplo:
 * 23.81 °C -> 23
 */
volatile int16_t mcp9808_temperature = 0;


/*
 * Temperatura en unidades de 1/16 °C.
 *
 * Ejemplo:
 *
 * 23.8125 °C
 *
 * 23.8125 * 16 = 381 = 0x017D
 */
volatile int16_t mcp9808_temperature_x16 = 0;


/*
 * Temperatura en centésimas de grado Celsius.
 *
 * Ejemplo:
 *
 * 23.81 °C -> 2381
 */
volatile int16_t mcp9808_temperature_centi = 0;


/*
 * Estado de comunicación.
 */
volatile bool mcp9808_ok = false;
volatile bool mcp9808_finished = false;


/*
 * Error del bus I2C.
 */
volatile i2c_host_error_t mcp9808_error = I2C_ERROR_NONE;


/* =========================================================
 * LEER TEMPERATURA DEL MCP9808
 * ========================================================= */

static bool MCP9808_Read(void)
{
    uint8_t register_address;
    uint32_t timeout;

    uint16_t raw_temperature;
    uint16_t temperature_raw;


    /* -----------------------------------------------------
     * Registro que queremos leer
     * ----------------------------------------------------- */

    register_address = MCP9808_TEMP_REGISTER;


    /* -----------------------------------------------------
     * Estado inicial
     * ----------------------------------------------------- */

    mcp9808_finished = false;
    mcp9808_ok = false;
    mcp9808_error = I2C_ERROR_NONE;


    /* -----------------------------------------------------
     * Limpiar datos anteriores
     * ----------------------------------------------------- */

    mcp9808_data[0] = 0;
    mcp9808_data[1] = 0;


    /* -----------------------------------------------------
     * I2C:
     *
     * 1. Escribir registro 0x05
     * 2. Leer 2 bytes
     * ----------------------------------------------------- */

    if (!I2C1_WriteRead(
            MCP9808_ADDRESS,
            &register_address,
            1,
            mcp9808_data,
            2))
    {
        /*
         * La transferencia no pudo iniciarse.
         */

        mcp9808_error = I2C1_ErrorGet();
        mcp9808_finished = true;

        return false;
    }


    /* -----------------------------------------------------
     * Esperar a que termine I2C
     * ----------------------------------------------------- */

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
     * Unir los dos bytes recibidos
     *
     * Ejemplo:
     *
     * data[0] = 0xC1
     * data[1] = 0x7D
     *
     * raw = 0xC17D
     * ----------------------------------------------------- */

    raw_temperature =
        ((uint16_t)mcp9808_data[0] << 8) |
        (uint16_t)mcp9808_data[1];


    /* -----------------------------------------------------
     * El MCP9808 tiene bits de estado en los bits 15:13.
     *
     * La temperatura está en los bits 12:0.
     * ----------------------------------------------------- */

    temperature_raw = raw_temperature & 0x1FFFU;


    /* -----------------------------------------------------
     * Comprobar signo.
     *
     * Bit 12 = 1 -> temperatura negativa
     * Bit 12 = 0 -> temperatura positiva
     * ----------------------------------------------------- */

    if ((temperature_raw & 0x1000U) != 0U)
    {
        /*
         * Temperatura negativa.
         *
         * El valor útil queda en los bits 11:0.
         */

        temperature_raw &= 0x0FFFU;


        /*
         * Guardar temperatura en unidades de 1/16 °C.
         *
         * Para temperatura negativa:
         *
         * -(4096 - valor)
         */

        mcp9808_temperature_x16 =
            -(int16_t)(4096U - temperature_raw);
    }
    else
    {
        /*
         * Temperatura positiva.
         */

        mcp9808_temperature_x16 =
            (int16_t)temperature_raw;
    }


    /* -----------------------------------------------------
     * Convertir a grados Celsius enteros.
     *
     * 381 / 16 = 23
     * ----------------------------------------------------- */

    mcp9808_temperature =
        mcp9808_temperature_x16 / 16;


    /* -----------------------------------------------------
     * Convertir a centésimas de grado.
     *
     * x16 contiene la temperatura multiplicada por 16.
     *
     * Celsius * 100 =
     *
     * x16 * 100 / 16
     *
     * Ejemplo:
     *
     * 381 * 100 / 16
     * = 2381
     * = 23.81 °C
     * ----------------------------------------------------- */

    mcp9808_temperature_centi =
        (int16_t)(((int32_t)mcp9808_temperature_x16 * 100L) / 16L);


    /* -----------------------------------------------------
     * Comunicación correcta
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
    /* -----------------------------------------------------
     * Inicialización de MCC
     * ----------------------------------------------------- */

    SYSTEM_Initialize();


    /* -----------------------------------------------------
     * Inicialización del I2C MSSP1
     * ----------------------------------------------------- */

    I2C1_Initialize();


    /* -----------------------------------------------------
     * Inicializar variables
     * ----------------------------------------------------- */

    mcp9808_data[0] = 0;
    mcp9808_data[1] = 0;

    mcp9808_temperature = 0;
    mcp9808_temperature_x16 = 0;
    mcp9808_temperature_centi = 0;

    mcp9808_ok = false;
    mcp9808_finished = false;

    mcp9808_error = I2C_ERROR_NONE;


    /* -----------------------------------------------------
     * Habilitar interrupciones.
     *
     * IMPORTANTE:
     *
     * Para el PIC16F13145 se utiliza INTCONbits.
     *
     * NO utilizar:
     *
     * INTCON0bits
     * ----------------------------------------------------- */

    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;


    /* -----------------------------------------------------
     * Bucle principal
     * ----------------------------------------------------- */

    while (1)
    {
        /* -------------------------------------------------
         * Leer MCP9808
         * ------------------------------------------------- */

        MCP9808_Read();


        /* -------------------------------------------------
         * Pequeña espera entre mediciones.
         *
         * Esto evita realizar lecturas continuamente.
         * ------------------------------------------------- */

        for (volatile uint32_t delay = 0;
             delay < 50000UL;
             delay++)
        {
            ;
        }
    }
}