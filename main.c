#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/mssp1.h"


/* =========================================================
 * MCP9808 - TEMPERATURA
 * ========================================================= */

#define MCP9808_ADDRESS       0x1C
#define MCP9808_TEMP_REGISTER 0x05


volatile uint8_t mcp9808_data[2] = {0, 0};

volatile int16_t mcp9808_temperature = 0;

volatile bool mcp9808_ok = false;
volatile bool mcp9808_finished = false;

volatile i2c_host_error_t mcp9808_error = I2C_ERROR_NONE;


/* =========================================================
 * VCNL4200 - LUZ Y PROXIMIDAD
 * ========================================================= */

#define VCNL4200_ADDRESS      0x51

#define VCNL4200_ALS_CONF     0x00
#define VCNL4200_PS_CONF1     0x03
#define VCNL4200_PS_CONF3     0x04

#define VCNL4200_PS_DATA      0x08
#define VCNL4200_ALS_DATA     0x09
#define VCNL4200_ID           0x0E


/* Datos crudos del VCNL4200 */

volatile uint8_t vcnl4200_ps_data[2] = {0, 0};
volatile uint8_t vcnl4200_als_data[2] = {0, 0};

volatile uint8_t vcnl4200_id_data[2] = {0, 0};


/* Valores calculados */

volatile uint16_t vcnl4200_proximity = 0;
volatile uint16_t vcnl4200_ambient_light = 0;


/*
 * Para ALS = 50 ms:
 *
 * 1 cuenta = 0.024 lux
 *
 * Para evitar float, guardamos el resultado
 * en mililux:
 *
 * lux x 1000
 */
volatile uint32_t vcnl4200_lux_millilux = 0;


/* Estado */

volatile bool vcnl4200_ok = false;
volatile bool vcnl4200_finished = false;

volatile i2c_host_error_t vcnl4200_error = I2C_ERROR_NONE;


/* =========================================================
 * MCP9808 - LEER TEMPERATURA
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


    mcp9808_error = I2C1_ErrorGet();

    if (mcp9808_error != I2C_ERROR_NONE)
    {
        mcp9808_finished = true;

        return false;
    }


    /*
     * Convertir MCP9808.
     *
     * La variable se mantiene en grados enteros.
     */

    {
        uint16_t raw_temperature;

        raw_temperature =
            ((uint16_t)mcp9808_data[0] << 8) |
            mcp9808_data[1];

        raw_temperature &= 0x0FFFU;


        if ((mcp9808_data[0] & 0x10U) != 0U)
        {
            mcp9808_temperature =
                -(int16_t)((4096U - raw_temperature) / 16U);
        }
        else
        {
            mcp9808_temperature =
                (int16_t)(raw_temperature / 16U);
        }
    }


    mcp9808_ok = true;
    mcp9808_finished = true;

    return true;
}


/* =========================================================
 * VCNL4200 - ESCRIBIR REGISTRO DE 16 BITS
 * ========================================================= */

static bool VCNL4200_WriteRegister(
        uint8_t reg,
        uint8_t data_low,
        uint8_t data_high)
{
    uint8_t tx_data[3];
    uint32_t timeout;


    tx_data[0] = reg;
    tx_data[1] = data_low;
    tx_data[2] = data_high;


    if (!I2C1_Write(
            VCNL4200_ADDRESS,
            tx_data,
            3))
    {
        vcnl4200_error = I2C1_ErrorGet();

        return false;
    }


    timeout = 0;

    while (I2C1_IsBusy())
    {
        timeout++;

        if (timeout > 100000UL)
        {
            vcnl4200_error =
                I2C_ERROR_BUS_COLLISION;

            return false;
        }
    }


    vcnl4200_error = I2C1_ErrorGet();

    if (vcnl4200_error != I2C_ERROR_NONE)
    {
        return false;
    }


    return true;
}


/* =========================================================
 * VCNL4200 - LEER REGISTRO DE 16 BITS
 * ========================================================= */

static bool VCNL4200_ReadRegister(
        uint8_t reg,
        uint8_t *data)
{
    uint32_t timeout;


    if (!I2C1_WriteRead(
            VCNL4200_ADDRESS,
            &reg,
            1,
            data,
            2))
    {
        vcnl4200_error = I2C1_ErrorGet();

        return false;
    }


    timeout = 0;

    while (I2C1_IsBusy())
    {
        timeout++;

        if (timeout > 100000UL)
        {
            vcnl4200_error =
                I2C_ERROR_BUS_COLLISION;

            return false;
        }
    }


    vcnl4200_error = I2C1_ErrorGet();

    if (vcnl4200_error != I2C_ERROR_NONE)
    {
        return false;
    }


    return true;
}


/* =========================================================
 * VCNL4200 - INICIALIZACIÓN
 * ========================================================= */

static bool VCNL4200_Initialize(void)
{
    vcnl4200_ok = false;
    vcnl4200_finished = false;
    vcnl4200_error = I2C_ERROR_NONE;


    /*
     * ALS_CONF
     *
     * LOW BYTE = 0x00
     *
     * ALS:
     * - 50 ms
     * - interrupt deshabilitada
     * - sensor encendido
     *
     * HIGH BYTE = 0x01
     * reservado según datasheet.
     */

    if (!VCNL4200_WriteRegister(
            VCNL4200_ALS_CONF,
            0x00,
            0x01))
    {
        vcnl4200_finished = true;

        return false;
    }


    /*
     * PS_CONF1
     *
     * LOW BYTE = 0x00
     *
     * - duty = 1/160
     * - persistence = 1
     * - integración = 1T
     * - proximity encendido
     *
     * HIGH BYTE = 0x00
     *
     * PS = 12 bits
     * interrupción deshabilitada.
     */

    if (!VCNL4200_WriteRegister(
            VCNL4200_PS_CONF1,
            0x00,
            0x00))
    {
        vcnl4200_finished = true;

        return false;
    }


    /*
     * PS_CONF3 / PS_MS
     *
     * Dejamos las opciones adicionales
     * en su configuración básica.
     */

    if (!VCNL4200_WriteRegister(
            VCNL4200_PS_CONF3,
            0x00,
            0x00))
    {
        vcnl4200_finished = true;

        return false;
    }


    /*
     * Leer ID para comprobar que realmente
     * tenemos comunicación con el VCNL4200.
     *
     * El datasheet indica:
     *
     * ID_L = 0x58
     * ID_H = 0x10
     */

    vcnl4200_id_data[0] = 0;
    vcnl4200_id_data[1] = 0;


    if (!VCNL4200_ReadRegister(
            VCNL4200_ID,
            vcnl4200_id_data))
    {
        vcnl4200_finished = true;

        return false;
    }


    /*
     * Comprobación de identificación.
     */

    if ((vcnl4200_id_data[0] == 0x58U) &&
        (vcnl4200_id_data[1] == 0x10U))
    {
        vcnl4200_ok = true;
    }
    else
    {
        vcnl4200_ok = false;
    }


    vcnl4200_finished = true;

    return vcnl4200_ok;
}


/* =========================================================
 * VCNL4200 - LEER LUZ
 * ========================================================= */

static bool VCNL4200_ReadAmbientLight(void)
{
    uint16_t raw_als;


    if (!VCNL4200_ReadRegister(
            VCNL4200_ALS_DATA,
            vcnl4200_als_data))
    {
        return false;
    }


    /*
     * El VCNL4200 entrega:
     *
     * ALS_Data_L = byte bajo
     * ALS_Data_H = byte alto
     */

    raw_als =
        ((uint16_t)vcnl4200_als_data[1] << 8) |
        vcnl4200_als_data[0];


    vcnl4200_ambient_light = raw_als;


    /*
     * ALS = 50 ms
     *
     * Resolución = 0.024 lux/step
     *
     * Guardamos:
     *
     * lux * 1000
     *
     * Por lo tanto:
     *
     * 1 cuenta = 24 mililux
     */

    vcnl4200_lux_millilux =
        (uint32_t)raw_als * 24UL;


    return true;
}


/* =========================================================
 * VCNL4200 - LEER PROXIMIDAD
 * ========================================================= */

static bool VCNL4200_ReadProximity(void)
{
    uint16_t raw_ps;


    if (!VCNL4200_ReadRegister(
            VCNL4200_PS_DATA,
            vcnl4200_ps_data))
    {
        return false;
    }


    raw_ps =
        ((uint16_t)vcnl4200_ps_data[1] << 8) |
        vcnl4200_ps_data[0];


    /*
     * Configuramos PS en 12 bits.
     */

    raw_ps &= 0x0FFFU;


    vcnl4200_proximity = raw_ps;


    return true;
}


/* =========================================================
 * VCNL4200 - LEER TODO
 * ========================================================= */

static bool VCNL4200_Read(void)
{
    bool light_ok;
    bool proximity_ok;


    light_ok =
        VCNL4200_ReadAmbientLight();


    proximity_ok =
        VCNL4200_ReadProximity();


    return (light_ok && proximity_ok);
}


/* =========================================================
 * DELAY SIMPLE
 * ========================================================= */

static void Delay_Long(void)
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
     * Inicialización MCC.
     */

    SYSTEM_Initialize();


    /*
     * Inicialización I2C.
     */

    I2C1_Initialize();


    /*
     * Variables MCP9808.
     */

    mcp9808_data[0] = 0;
    mcp9808_data[1] = 0;

    mcp9808_temperature = 0;

    mcp9808_ok = false;
    mcp9808_finished = false;

    mcp9808_error = I2C_ERROR_NONE;


    /*
     * Variables VCNL4200.
     */

    vcnl4200_ps_data[0] = 0;
    vcnl4200_ps_data[1] = 0;

    vcnl4200_als_data[0] = 0;
    vcnl4200_als_data[1] = 0;

    vcnl4200_id_data[0] = 0;
    vcnl4200_id_data[1] = 0;

    vcnl4200_proximity = 0;

    vcnl4200_ambient_light = 0;

    vcnl4200_lux_millilux = 0;

    vcnl4200_ok = false;
    vcnl4200_finished = false;

    vcnl4200_error = I2C_ERROR_NONE;


    /*
     * Interrupciones del PIC.
     *
     * En tu PIC16F13145 el registro correcto
     * es INTCONbits.
     */

    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;


    /*
     * Inicializar VCNL4200.
     */

    VCNL4200_Initialize();


    /*
     * Bucle principal.
     */

    while (1)
    {
        /*
         * MCP9808
         */

        MCP9808_Read();


        /*
         * VCNL4200
         */

        VCNL4200_Read();


        /*
         * Esperar antes de repetir.
         */

        Delay_Long();
    }
}