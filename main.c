#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/mssp1.h"
#include "mcc_generated_files/uart/eusart1.h"


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

volatile int16_t mcp9808_temperature = 0;
volatile int16_t mcp9808_temperature_centi = 0;
volatile int16_t mcp9808_temperature_integer = 0;
volatile uint16_t mcp9808_temperature_decimal = 0;

volatile bool mcp9808_ok = false;
volatile i2c_host_error_t mcp9808_error = I2C_ERROR_NONE;


/* =========================================================
 * VARIABLES VCNL4200
 * ========================================================= */

volatile uint16_t vcnl4200_ambient_light = 0;

volatile uint32_t vcnl4200_lux_millilux = 0;
volatile uint32_t vcnl4200_lux_integer = 0;
volatile uint16_t vcnl4200_lux_decimal = 0;

volatile uint16_t vcnl4200_proximity = 0;

volatile uint8_t vcnl4200_id_data[2] = {0, 0};

volatile bool vcnl4200_ok = false;
volatile i2c_host_error_t vcnl4200_error = I2C_ERROR_NONE;


/* =========================================================
 * BUFFERS I2C
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
 * UART - ENVIAR UN CARACTER
 * ========================================================= */

static void UART_WriteChar(char c)
{
    while (!EUSART1_IsTxReady())
    {
        ;
    }

    EUSART1_Write((uint8_t)c);
}


/* =========================================================
 * UART - ENVIAR TEXTO
 * ========================================================= */

static void UART_WriteString(const char *text)
{
    while (*text != '\0')
    {
        UART_WriteChar(*text);
        text++;
    }
}


/* =========================================================
 * UART - ENVIAR NUMERO ENTERO POSITIVO
 * ========================================================= */

static void UART_WriteUInt16(uint16_t value)
{
    char buffer[6];
    uint8_t i = 0;

    if (value == 0)
    {
        UART_WriteChar('0');
        return;
    }

    while (value > 0)
    {
        buffer[i] = (char)('0' + (value % 10));
        value /= 10;
        i++;
    }

    while (i > 0)
    {
        i--;
        UART_WriteChar(buffer[i]);
    }
}


/* =========================================================
 * UART - ENVIAR NUMERO ENTERO 32 BITS
 * ========================================================= */

static void UART_WriteUInt32(uint32_t value)
{
    char buffer[11];
    uint8_t i = 0;

    if (value == 0)
    {
        UART_WriteChar('0');
        return;
    }

    while (value > 0)
    {
        buffer[i] = (char)('0' + (value % 10UL));
        value /= 10UL;
        i++;
    }

    while (i > 0)
    {
        i--;
        UART_WriteChar(buffer[i]);
    }
}


/* =========================================================
 * UART - ENVIAR TEMPERATURA
 * ========================================================= */

static void UART_WriteTemperature(void)
{
    UART_WriteString("Temperatura: ");

    if (mcp9808_temperature_centi < 0)
    {
        UART_WriteChar('-');
    }

    UART_WriteUInt16(
        (uint16_t)(
            mcp9808_temperature_integer < 0
            ? -mcp9808_temperature_integer
            : mcp9808_temperature_integer
        )
    );

    UART_WriteChar('.');

    if (mcp9808_temperature_decimal < 10)
    {
        UART_WriteChar('0');
    }

    UART_WriteUInt16(mcp9808_temperature_decimal);

    UART_WriteString(" C\r\n");
}


/* =========================================================
 * UART - ENVIAR LUZ
 * ========================================================= */

static void UART_WriteLight(void)
{
    UART_WriteString("Luz: ");

    UART_WriteUInt32(vcnl4200_lux_integer);

    UART_WriteChar('.');

    if (vcnl4200_lux_decimal < 10)
    {
        UART_WriteChar('0');
    }

    UART_WriteUInt16(vcnl4200_lux_decimal);

    UART_WriteString(" lux\r\n");
}


/* =========================================================
 * ESCRIBIR REGISTRO DE 16 BITS VCNL4200
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
 * LEER REGISTRO DE 16 BITS VCNL4200
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
     * VCNL4200 entrega primero byte LOW
     * y después byte HIGH.
     */

    *value =
        ((uint16_t)i2c_read_buffer[1] << 8) |
        i2c_read_buffer[0];

    return true;
}


/* =========================================================
 * LEER TEMPERATURA RAW MCP9808
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
     * MCP9808 entrega:
     *
     * Byte 0 = MSB
     * Byte 1 = LSB
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
     * Extraer los 13 bits de temperatura.
     */

    temperature_raw = raw & 0x1FFF;

    /*
     * Detectar temperatura negativa.
     */

    if (temperature_raw & 0x1000)
    {
        temperature_raw -= 0x2000;
    }

    /*
     * Resolución MCP9808:
     *
     * 0.25 °C por cuenta.
     *
     * Multiplicamos por 25 / 4
     * para obtener centésimas de grado.
     */

    centi =
        (int16_t)(((int32_t)temperature_raw * 25) / 4);

    mcp9808_temperature_centi = centi;

    /*
     * Parte entera.
     */

    mcp9808_temperature_integer =
        centi / 100;

    /*
     * Parte decimal.
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
     * Mantener también la temperatura entera.
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
     * Configurar ALS.
     *
     * ALS habilitado.
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
     * Configurar proximidad.
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

    /*
     * Leer luz ambiente.
     */

    if (!VCNL4200_ReadRegister16(
            VCNL4200_REG_ALS_DATA,
            &ambient))
    {
        return false;
    }

    vcnl4200_ambient_light = ambient;

    /*
     * Según la conversión utilizada:
     *
     * 1 cuenta = 0.024 lux
     *
     * 0.024 lux = 24 mililux.
     */

    vcnl4200_lux_millilux =
        (uint32_t)ambient * 24UL;

    /*
     * Parte entera.
     */

    vcnl4200_lux_integer =
        vcnl4200_lux_millilux / 1000UL;

    /*
     * Dos decimales.
     */

    vcnl4200_lux_decimal =
        (uint16_t)(
            (vcnl4200_lux_millilux % 1000UL) / 10UL
        );

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

    vcnl4200_id_data[0] =
        (uint8_t)(id & 0xFF);

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

    /*
     * =====================================================
     * 1. INICIALIZAR SISTEMA
     * =====================================================
     */

    SYSTEM_Initialize();


    /*
     * =====================================================
     * 2. INICIALIZAR I2C
     * =====================================================
     */

    I2C1_Initialize();


    /*
     * =====================================================
     * 3. INICIALIZAR EUSART1
     * =====================================================
     */

    EUSART1_Initialize();


    /*
     * =====================================================
     * 4. INICIALIZAR VARIABLES MCP9808
     * =====================================================
     */

    mcp9808_temperature = 0;
    mcp9808_temperature_centi = 0;
    mcp9808_temperature_integer = 0;
    mcp9808_temperature_decimal = 0;

    mcp9808_ok = false;
    mcp9808_error = I2C_ERROR_NONE;


    /*
     * =====================================================
     * 5. INICIALIZAR VARIABLES VCNL4200
     * =====================================================
     */

    vcnl4200_ambient_light = 0;

    vcnl4200_lux_millilux = 0;
    vcnl4200_lux_integer = 0;
    vcnl4200_lux_decimal = 0;

    vcnl4200_proximity = 0;

    vcnl4200_id_data[0] = 0;
    vcnl4200_id_data[1] = 0;

    vcnl4200_ok = false;
    vcnl4200_error = I2C_ERROR_NONE;


    /*
     * =====================================================
     * 6. HABILITAR INTERRUPCIONES
     * =====================================================
     */

    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;


    /*
     * =====================================================
     * 7. MENSAJE INICIAL POR UART
     * =====================================================
     */

    UART_WriteString(
        "\r\n================================\r\n"
    );

    UART_WriteString(
        "Estacion Monitoreo Ambiental\r\n"
    );

    UART_WriteString(
        "PIC16F13145\r\n"
    );

    UART_WriteString(
        "UART: 9600 8N1\r\n"
    );

    UART_WriteString(
        "================================\r\n\r\n"
    );


    /*
     * =====================================================
     * 8. INICIALIZAR VCNL4200
     * =====================================================
     */

    vcnl4200_ok =
        VCNL4200_Initialize();


    /*
     * =====================================================
     * 9. LEER ID DEL VCNL4200
     * =====================================================
     */

    if (vcnl4200_ok)
    {
        if (!VCNL4200_ReadID())
        {
            vcnl4200_ok = false;
        }
    }


    /*
     * =====================================================
     * 10. INFORMAR ESTADO DEL VCNL4200
     * =====================================================
     */

    if (vcnl4200_ok)
    {
        UART_WriteString(
            "VCNL4200: OK\r\n"
        );

        UART_WriteString(
            "ID LOW: 0x"
        );

        /*
         * El ID se conserva en las variables
         * para poder observarlo en el debugger.
         */
    }
    else
    {
        UART_WriteString(
            "VCNL4200: ERROR\r\n"
        );
    }


    /*
     * =====================================================
     * 11. BUCLE PRINCIPAL
     * =====================================================
     */

    while (1)
    {

        /*
         * =================================================
         * TEMPERATURA MCP9808
         * =================================================
         */

        if (MCP9808_ReadTemperatureRaw(
                &mcp9808_raw_temperature))
        {
            MCP9808_ProcessTemperature(
                mcp9808_raw_temperature
            );

            mcp9808_ok = true;
        }
        else
        {
            mcp9808_ok = false;
        }


        /*
         * =================================================
         * LUZ AMBIENTE VCNL4200
         * =================================================
         */

        if (VCNL4200_ReadSensors())
        {
            vcnl4200_ok = true;
        }
        else
        {
            vcnl4200_ok = false;
        }


        /*
         * =================================================
         * ENVIAR DATOS POR UART
         * =================================================
         */

        if (mcp9808_ok)
        {
            UART_WriteTemperature();
        }
        else
        {
            UART_WriteString(
                "Temperatura: ERROR\r\n"
            );
        }


        if (vcnl4200_ok)
        {
            UART_WriteLight();
        }
        else
        {
            UART_WriteString(
                "Luz: ERROR\r\n"
            );
        }


        /*
         * Separador.
         */

        UART_WriteString(
            "-----------------------------\r\n"
        );


        /*
         * Pequeña espera.
         */

        Delay_Long();
    }
}
