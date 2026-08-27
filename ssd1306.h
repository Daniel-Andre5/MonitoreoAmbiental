#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/mssp1.h"


/* =========================================================
 * DIRECCIONES I2C
 * ========================================================= */

#define MCP9808_I2C_ADDRESS       0x1C
#define VCNL4200_I2C_ADDRESS      0x51


/* =========================================================
 * REGISTROS MCP9808
 * ========================================================= */

#define MCP9808_REG_TEMPERATURE   0x05


/* =========================================================
 * REGISTROS VCNL4200
 * ========================================================= */

#define VCNL4200_REG_ALS_CONF     0x00
#define VCNL4200_REG_PS_CONF      0x03
#define VCNL4200_REG_PS_DATA      0x08
#define VCNL4200_REG_ALS_DATA     0x09
#define VCNL4200_REG_ID           0x0E


/* =========================================================
 * VARIABLES MCP9808
 * ========================================================= */

/* Temperatura en grados enteros */
volatile int16_t mcp9808_temperature = 0;

/* Temperatura completa en centésimas de grado */
volatile int16_t mcp9808_temperature_centi = 0;

/* Parte entera */
volatile int16_t mcp9808_temperature_integer = 0;

/*
 * Parte decimal.
 *
 * IMPORTANTE:
 * Ahora es uint16_t y no uint8_t para que MPLAB
 * la muestre como número y no como ASCII.
 *
 * Ejemplo:
 *
 * 23.25 °C
 *
 * temperature_integer = 23
 * temperature_decimal = 25
 */
volatile uint16_t mcp9808_temperature_decimal = 0;

/* Estado del MCP9808 */
volatile bool mcp9808_ok = false;

/* Error I2C del MCP9808 */
volatile i2c_host_error_t mcp9808_error = I2C_ERROR_NONE;


/* =========================================================
 * VARIABLES VCNL4200
 * ========================================================= */

/* Lectura ADC del sensor de luz */
volatile uint16_t vcnl4200_ambient_light = 0;

/*
 * Lux en mililux.
 *
 * Ejemplo:
 *
 * 333.12 lux
 *
 * = 333120 mililux
 */
volatile uint32_t vcnl4200_lux_millilux = 0;

/* Parte entera de lux */
volatile uint32_t vcnl4200_lux_integer = 0;

/*
 * Parte decimal de lux.
 *
 * Ahora uint16_t para que MPLAB lo muestre
 * como número.
 *
 * Ejemplo:
 *
 * 333.12 lux
 *
 * lux_integer = 333
 * lux_decimal = 12
 */
volatile uint16_t vcnl4200_lux_decimal = 0;

/* Proximidad */
volatile uint16_t vcnl4200_proximity = 0;

/* ID del VCNL4200 */
volatile uint8_t vcnl4200_id_data[2] = {0, 0};

/* Estado del VCNL4200 */
volatile bool vcnl4200_ok = false;

/* Error I2C del VCNL4200 */
volatile i2c_host_error_t vcnl4200_error = I2C_ERROR_NONE;


/* =========================================================
 * BUFFER I2C
 * ========================================================= */

static uint8_t i2c_write_buffer[3];
static uint8_t i2c_read_buffer[2];


/* =========================================================
 * RETARDO
 * ========================================================= */

static void Delay_Long(void)
{
    volatile uint32_t delay;

    for (delay = 0; delay < 50000UL; delay++)
    {
        ;
    }
}


/* =========================================================
 * ESPERAR A QUE TERMINE I2C
 * ========================================================= */

static bool I2C_WaitComplete(void)
{
    volatile uint32_t timeout = 0;

    while (I2C1_IsBusy())
    {
        timeout++;

        if (timeout > 100000UL)
        {
            return false;
        }
    }

    return true;
}


/* =========================================================
 * ESCRIBIR REGISTRO DE 16 BITS
 *
 * VCNL4200:
 *
 * registro
 * byte bajo
 * byte alto
 * ========================================================= */

static bool VCNL4200_WriteRegister16(uint8_t reg,
                                     uint8_t low,
                                     uint8_t high)
{
    i2c_write_buffer[0] = reg;
    i2c_write_buffer[1] = low;
    i2c_write_buffer[2] = high;

    if (!I2C1_Write(VCNL4200_I2C_ADDRESS,
                    i2c_write_buffer,
                    3))
    {
        return false;
    }

    if (!I2C_WaitComplete())
    {
        return false;
    }

    vcnl4200_error = I2C1_ErrorGet();

    if (vcnl4200_error != I2C_ERROR_NONE)
    {
        return false;
    }

    return true;
}


/* =========================================================
 * LEER REGISTRO DE 16 BITS DEL VCNL4200
 * ========================================================= */

static bool VCNL4200_ReadRegister16(uint8_t reg,
                                    uint16_t *value)
{
    i2c_write_buffer[0] = reg;

    i2c_read_buffer[0] = 0;
    i2c_read_buffer[1] = 0;

    if (!I2C1_WriteRead(VCNL4200_I2C_ADDRESS,
                        i2c_write_buffer,
                        1,
                        i2c_read_buffer,
                        2))
    {
        return false;
    }

    if (!I2C_WaitComplete())
    {
        return false;
    }

    vcnl4200_error = I2C1_ErrorGet();

    if (vcnl4200_error != I2C_ERROR_NONE)
    {
        return false;
    }

    /*
     * VCNL4200:
     *
     * byte bajo primero
     * byte alto después
     */

    *value =
        ((uint16_t)i2c_read_buffer[1] << 8) |
        i2c_read_buffer[0];

    return true;
}


/* =========================================================
 * LEER TEMPERATURA RAW DEL MCP9808
 * ========================================================= */

static bool MCP9808_ReadTemperatureRaw(uint16_t *value)
{
    i2c_write_buffer[0] = MCP9808_REG_TEMPERATURE;

    i2c_read_buffer[0] = 0;
    i2c_read_buffer[1] = 0;

    if (!I2C1_WriteRead(MCP9808_I2C_ADDRESS,
                        i2c_write_buffer,
                        1,
                        i2c_read_buffer,
                        2))
    {
        return false;
    }

    if (!I2C_WaitComplete())
    {
        return false;
    }

    mcp9808_error = I2C1_ErrorGet();

    if (mcp9808_error != I2C_ERROR_NONE)
    {
        return false;
    }

    /*
     * MCP9808:
     *
     * MSB primero
     * LSB después
     */

    *value =
        ((uint16_t)i2c_read_buffer[0] << 8) |
        i2c_read_buffer[1];

    return true;
}


/* =========================================================
 * PROCESAR TEMPERATURA MCP9808
 * ========================================================= */

static void MCP9808_ProcessTemperature(uint16_t raw)
{
    int16_t temperature_raw;
    int16_t centi;


    /*
     * El MCP9808 utiliza 13 bits:
     *
     * bits 12:0 = temperatura
     *
     * bit 12 = signo
     */

    temperature_raw = raw & 0x1FFF;


    /*
     * Convertir complemento a dos
     * de 13 bits a entero con signo.
     */

    if (temperature_raw & 0x1000)
    {
        temperature_raw -= 0x2000;
    }


    /*
     * Cada cuenta = 0.0625 °C
     *
     * Para centésimas:
     *
     * 0.0625 x 100 = 6.25
     *
     * 6.25 = 25 / 4
     */

    centi =
        (int16_t)(((int32_t)temperature_raw * 25) / 4);


    /*
     * Ejemplo:
     *
     * 23.25 °C
     *
     * centi = 2325
     */

    mcp9808_temperature_centi = centi;


    /*
     * Parte entera.
     *
     * 2325 / 100 = 23
     */

    mcp9808_temperature_integer =
        centi / 100;


    /*
     * Parte decimal.
     *
     * 2325 % 100 = 25
     */

    if (centi >= 0)
    {
        mcp9808_temperature_decimal =
            (uint16_t)(centi % 100);
    }
    else
    {
        mcp9808_temperature_decimal =
            (uint16_t)((-centi) % 100);
    }


    /*
     * Temperatura entera.
     */

    mcp9808_temperature =
        mcp9808_temperature_integer;
}


/* =========================================================
 * CONFIGURAR VCNL4200
 * ========================================================= */

static bool VCNL4200_Initialize(void)
{
    bool ok;


    /*
     * -----------------------------------------------------
     * ALS CONFIGURATION
     * -----------------------------------------------------
     */

    ok = VCNL4200_WriteRegister16(
        VCNL4200_REG_ALS_CONF,
        0x00,
        0x00
    );

    if (!ok)
    {
        return false;
    }


    /*
     * -----------------------------------------------------
     * PROXIMITY CONFIGURATION
     * -----------------------------------------------------
     */

    ok = VCNL4200_WriteRegister16(
        VCNL4200_REG_PS_CONF,
        0x00,
        0x00
    );

    if (!ok)
    {
        return false;
    }


    return true;
}


/* =========================================================
 * LEER SENSORES VCNL4200
 * ========================================================= */

static bool VCNL4200_ReadSensors(void)
{
    uint16_t ambient;
    uint16_t proximity;


    /*
     * -----------------------------------------------------
     * LEER LUZ AMBIENTE
     * -----------------------------------------------------
     */

    if (!VCNL4200_ReadRegister16(
            VCNL4200_REG_ALS_DATA,
            &ambient))
    {
        return false;
    }


    vcnl4200_ambient_light = ambient;


    /*
     * -----------------------------------------------------
     * CONVERTIR A LUX
     * -----------------------------------------------------
     *
     * Con 50 ms:
     *
     * 1 cuenta = 0.024 lux
     *
     * En mililux:
     *
     * 0.024 lux = 24 mililux
     *
     * Entonces:
     *
     * mililux = ADC x 24
     */

    vcnl4200_lux_millilux =
        (uint32_t)ambient * 24UL;


    /*
     * Parte entera.
     *
     * Ejemplo:
     *
     * 333120 mililux
     *
     * = 333 lux
     */

    vcnl4200_lux_integer =
        vcnl4200_lux_millilux / 1000UL;


    /*
     * Parte decimal.
     *
     * Queremos dos decimales.
     *
     * Ejemplo:
     *
     * 333120
     *
     * resto = 120
     *
     * 120 / 10 = 12
     *
     * Resultado:
     *
     * 333.12 lux
     */

    vcnl4200_lux_decimal =
        (uint16_t)(
            (vcnl4200_lux_millilux % 1000UL) / 10UL
        );


    /*
     * -----------------------------------------------------
     * LEER PROXIMIDAD
     * -----------------------------------------------------
     */

    if (!VCNL4200_ReadRegister16(
            VCNL4200_REG_PS_DATA,
            &proximity))
    {
        return false;
    }


    vcnl4200_proximity = proximity;


    return true;
}


/* =========================================================
 * LEER ID VCNL4200
 * ========================================================= */

static bool VCNL4200_ReadID(void)
{
    uint16_t id;


    if (!VCNL4200_ReadRegister16(
            VCNL4200_REG_ID,
            &id))
    {
        return false;
    }


    /*
     * Byte bajo
     */

    vcnl4200_id_data[0] =
        (uint8_t)(id & 0xFF);


    /*
     * Byte alto
     */

    vcnl4200_id_data[1] =
        (uint8_t)((id >> 8) & 0xFF);


    return true;
}


/* =========================================================
 * MAIN
 * ========================================================= */

void main(void)
{
    uint16_t mcp9808_raw_temperature;


    /* =====================================================
     * INICIALIZAR SISTEMA
     * ===================================================== */

    SYSTEM_Initialize();


    /* =====================================================
     * INICIALIZAR I2C
     * ===================================================== */

    I2C1_Initialize();


    /* =====================================================
     * INICIALIZAR VARIABLES MCP9808
     * ===================================================== */

    mcp9808_temperature = 0;

    mcp9808_temperature_centi = 0;

    mcp9808_temperature_integer = 0;

    mcp9808_temperature_decimal = 0;

    mcp9808_ok = false;

    mcp9808_error = I2C_ERROR_NONE;


    /* =====================================================
     * INICIALIZAR VARIABLES VCNL4200
     * ===================================================== */

    vcnl4200_ambient_light = 0;

    vcnl4200_lux_millilux = 0;

    vcnl4200_lux_integer = 0;

    vcnl4200_lux_decimal = 0;

    vcnl4200_proximity = 0;

    vcnl4200_id_data[0] = 0;

    vcnl4200_id_data[1] = 0;

    vcnl4200_ok = false;

    vcnl4200_error = I2C_ERROR_NONE;


    /* =====================================================
     * INTERRUPCIONES
     * ===================================================== */

    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;


    /* =====================================================
     * INICIALIZAR VCNL4200
     * ===================================================== */

    vcnl4200_ok =
        VCNL4200_Initialize();


    /* =====================================================
     * LEER ID DEL VCNL4200
     * ===================================================== */

    if (vcnl4200_ok)
    {
        if (!VCNL4200_ReadID())
        {
            vcnl4200_ok = false;
        }
    }


    /* =====================================================
     * BUCLE PRINCIPAL
     * ===================================================== */

    while (1)
    {

        /* =================================================
         * MCP9808
         * ================================================= */

        if (MCP9808_ReadTemperatureRaw(
                &mcp9808_raw_temperature))
        {
            /*
             * Convertir la lectura raw
             * a temperatura.
             */

            MCP9808_ProcessTemperature(
                mcp9808_raw_temperature
            );


            /*
             * Sensor funcionando correctamente.
             */

            mcp9808_ok = true;
        }
        else
        {
            /*
             * Error de comunicación.
             */

            mcp9808_ok = false;
        }


        /* =================================================
         * VCNL4200
         * ================================================= */

        if (VCNL4200_ReadSensors())
        {
            /*
             * Lectura correcta.
             */

            vcnl4200_ok = true;
        }
        else
        {
            /*
             * Error de comunicación.
             */

            vcnl4200_ok = false;
        }


        /* =================================================
         * PEQUEÑA ESPERA
         * ================================================= */

        Delay_Long();
    }
}