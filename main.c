/*
 * ============================================================
 * ESTACION DE MONITOREO AMBIENTAL
 * PIC16F13145
 *
 * MCP9808  -> Temperatura
 * VCNL4200 -> Luz ambiental
 * EUSART1  -> USB Bridge -> PuTTY
 *
 * UART:
 * 9600 baudios
 * 8 bits
 * Sin paridad
 * 1 bit de parada
 * Sin control de flujo
 *
 * ACTUALIZACION:
 * Cada 3 segundos
 *
 * La pantalla de PuTTY se limpia antes de cada lectura,
 * por lo que solamente se muestra una lectura a la vez.
 * ============================================================
 */

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/mssp1.h"
#include "mcc_generated_files/uart/eusart1.h"


/* ============================================================
 * DIRECCIONES I2C
 * ============================================================
 */

#define MCP9808_I2C_ADDRESS       0x1C
#define VCNL4200_I2C_ADDRESS      0x51


/* ============================================================
 * REGISTROS MCP9808
 * ============================================================
 */

#define MCP9808_REG_TEMPERATURE   0x05


/* ============================================================
 * REGISTROS VCNL4200
 * ============================================================
 */

#define VCNL4200_REG_ALS_CONF     0x00
#define VCNL4200_REG_PS_CONF      0x03
#define VCNL4200_REG_PS_DATA      0x08
#define VCNL4200_REG_ALS_DATA     0x09
#define VCNL4200_REG_ID           0x0E


/* ============================================================
 * VARIABLES MCP9808
 * ============================================================
 */

volatile int16_t mcp9808_temperature = 0;
volatile int16_t mcp9808_temperature_centi = 0;
volatile int16_t mcp9808_temperature_integer = 0;
volatile uint16_t mcp9808_temperature_decimal = 0;

volatile bool mcp9808_ok = false;
volatile i2c_host_error_t mcp9808_error = I2C_ERROR_NONE;


/* ============================================================
 * VARIABLES VCNL4200
 * ============================================================
 */

volatile uint16_t vcnl4200_ambient_light = 0;

volatile uint32_t vcnl4200_lux_millilux = 0;
volatile uint32_t vcnl4200_lux_integer = 0;
volatile uint16_t vcnl4200_lux_decimal = 0;

volatile uint16_t vcnl4200_proximity = 0;

volatile uint8_t vcnl4200_id_data[2] = {0, 0};

volatile bool vcnl4200_ok = false;
volatile i2c_host_error_t vcnl4200_error = I2C_ERROR_NONE;


/* ============================================================
 * BUFFERS I2C
 * ============================================================
 */

static uint8_t i2c_write_buffer[3];
static uint8_t i2c_read_buffer[2];


/* ============================================================
 * UART - ENVIAR CARACTER
 * ============================================================
 */

static void UART_WriteChar(char c)
{
    while (!EUSART1_IsTxReady())
    {
        ;
    }

    EUSART1_Write((uint8_t)c);
}


/* ============================================================
 * UART - ENVIAR TEXTO
 * ============================================================
 */

static void UART_WriteString(const char *text)
{
    while (*text != '\0')
    {
        UART_WriteChar(*text);
        text++;
    }
}


/* ============================================================
 * LIMPIAR PANTALLA DE PUTTY
 *
 * ESC [ 2 J
 * ESC [ H
 *
 * Esto funciona en terminales que soportan ANSI.
 * PuTTY soporta estas secuencias.
 * ============================================================
 */

static void UART_ClearScreen(void)
{
    UART_WriteChar(0x1B);
    UART_WriteChar('[');
    UART_WriteString("2J");

    UART_WriteChar(0x1B);
    UART_WriteChar('[');
    UART_WriteChar('H');
}


/* ============================================================
 * UART - ENVIAR UINT16
 * ============================================================
 */

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


/* ============================================================
 * UART - ENVIAR UINT32
 * ============================================================
 */

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


/* ============================================================
 * UART - TEMPERATURA
 * ============================================================
 */

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


/* ============================================================
 * UART - LUZ
 * ============================================================
 */

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


/* ============================================================
 * ESPERAR I2C
 * ============================================================
 */

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


/* ============================================================
 * VCNL4200 - ESCRIBIR REGISTRO 16 BITS
 * ============================================================
 */

static bool VCNL4200_WriteRegister16(uint8_t reg,
                                     uint8_t low,
                                     uint8_t high)
{
    i2c_write_buffer[0] = reg;
    i2c_write_buffer[1] = low;
    i2c_write_buffer[2] = high;

    if (!I2C1_Write(
            VCNL4200_I2C_ADDRESS,
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


/* ============================================================
 * VCNL4200 - LEER REGISTRO 16 BITS
 * ============================================================
 */

static bool VCNL4200_ReadRegister16(uint8_t reg,
                                    uint16_t *value)
{
    i2c_write_buffer[0] = reg;

    i2c_read_buffer[0] = 0;
    i2c_read_buffer[1] = 0;

    if (!I2C1_WriteRead(
            VCNL4200_I2C_ADDRESS,
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
     * byte 0 = LOW
     * byte 1 = HIGH
     */

    *value =
        ((uint16_t)i2c_read_buffer[1] << 8) |
        i2c_read_buffer[0];

    return true;
}


/* ============================================================
 * MCP9808 - LEER TEMPERATURA RAW
 * ============================================================
 */

static bool MCP9808_ReadTemperatureRaw(uint16_t *value)
{
    i2c_write_buffer[0] =
        MCP9808_REG_TEMPERATURE;

    i2c_read_buffer[0] = 0;
    i2c_read_buffer[1] = 0;

    if (!I2C1_WriteRead(
            MCP9808_I2C_ADDRESS,
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
     * byte 0 = MSB
     * byte 1 = LSB
     */

    *value =
        ((uint16_t)i2c_read_buffer[0] << 8) |
        i2c_read_buffer[1];

    return true;
}


/* ============================================================
 * MCP9808 - PROCESAR TEMPERATURA
 * ============================================================
 */

static void MCP9808_ProcessTemperature(uint16_t raw)
{
    int16_t temperature_raw;
    int16_t centi;

    /*
     * Extraer 13 bits de temperatura.
     */

    temperature_raw =
        raw & 0x1FFF;

    /*
     * Temperatura negativa.
     */

    if (temperature_raw & 0x1000)
    {
        temperature_raw -= 0x2000;
    }

    /*
     * MCP9808:
     *
     * 1 cuenta = 0.25 C
     *
     * En centesimas:
     *
     * temperatura * 25 / 4
     */

    centi =
        (int16_t)(
            ((int32_t)temperature_raw * 25) / 4
        );

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

    mcp9808_temperature =
        mcp9808_temperature_integer;
}


/* ============================================================
 * VCNL4200 - INICIALIZACION
 * ============================================================
 */

static bool VCNL4200_Initialize(void)
{
    bool ok;

    /*
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
     * Proximidad.
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


/* ============================================================
 * VCNL4200 - LEER SENSORES
 * ============================================================
 */

static bool VCNL4200_ReadSensors(void)
{
    uint16_t ambient;

    /*
     * Leer ALS.
     */

    if (!VCNL4200_ReadRegister16(
            VCNL4200_REG_ALS_DATA,
            &ambient))
    {
        return false;
    }

    vcnl4200_ambient_light =
        ambient;

    /*
     * Conversion:
     *
     * 1 cuenta = 0.024 lux
     *
     * 0.024 lux = 24 mililux
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
            (vcnl4200_lux_millilux % 1000UL)
            / 10UL
        );

    return true;
}


/* ============================================================
 * VCNL4200 - LEER ID
 * ============================================================
 */

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


/* ============================================================
 * RETARDO DE 3 SEGUNDOS
 *
 * IMPORTANTE:
 *
 * Esta función usa __delay_ms().
 *
 * XC8 necesita conocer _XTAL_FREQ.
 *
 * Si MCC ya define _XTAL_FREQ en system.h,
 * se utilizará esa frecuencia.
 * ============================================================
 */

static void Delay_3_Seconds(void)
{
    uint8_t i;

    for (i = 0; i < 3; i++)
    {
        __delay_ms(1000);
    }
}


/* ============================================================
 * MAIN
 * ============================================================
 */

void main(void)
{
    uint16_t mcp9808_raw_temperature;

    /*
     * --------------------------------------------------------
     * Inicializar sistema
     * --------------------------------------------------------
     */

    SYSTEM_Initialize();


    /*
     * --------------------------------------------------------
     * Inicializar I2C
     * --------------------------------------------------------
     */

    I2C1_Initialize();


    /*
     * --------------------------------------------------------
     * Inicializar UART
     * --------------------------------------------------------
     */

    EUSART1_Initialize();


    /*
     * --------------------------------------------------------
     * Habilitar interrupciones
     * --------------------------------------------------------
     */

    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;


    /*
     * --------------------------------------------------------
     * Mensaje inicial
     * --------------------------------------------------------
     */

    UART_ClearScreen();

    UART_WriteString(
        "================================\r\n"
    );

    UART_WriteString(
        " ESTACION DE MONITOREO AMBIENTAL\r\n"
    );

    UART_WriteString(
        " PIC16F13145\r\n"
    );

    UART_WriteString(
        " UART: 9600 8N1\r\n"
    );

    UART_WriteString(
        "================================\r\n"
    );

    UART_WriteString(
        "Inicializando sensores...\r\n"
    );


    /*
     * --------------------------------------------------------
     * Inicializar VCNL4200
     * --------------------------------------------------------
     */

    vcnl4200_ok =
        VCNL4200_Initialize();


    /*
     * --------------------------------------------------------
     * Leer ID
     * --------------------------------------------------------
     */

    if (vcnl4200_ok)
    {
        if (!VCNL4200_ReadID())
        {
            vcnl4200_ok = false;
        }
    }


    /*
     * --------------------------------------------------------
     * Mostrar estado
     * --------------------------------------------------------
     */

    UART_WriteString("\r\n");

    if (vcnl4200_ok)
    {
        UART_WriteString(
            "VCNL4200: OK\r\n"
        );
    }
    else
    {
        UART_WriteString(
            "VCNL4200: ERROR\r\n"
        );
    }


    UART_WriteString(
        "MCP9808: preparado\r\n"
    );

    UART_WriteString(
        "\r\nComenzando monitoreo...\r\n"
    );


    /*
     * --------------------------------------------------------
     * Pequeña espera inicial
     * --------------------------------------------------------
     */

    Delay_3_Seconds();


    /*
     * ========================================================
     * BUCLE PRINCIPAL
     * ========================================================
     */

    while (1)
    {

        /*
         * ----------------------------------------------------
         * LIMPIAR PANTALLA
         * ----------------------------------------------------
         */

        UART_ClearScreen();


        /*
         * ----------------------------------------------------
         * ENCABEZADO
         * ----------------------------------------------------
         */

        UART_WriteString(
            "================================\r\n"
        );

        UART_WriteString(
            "   MONITOREO AMBIENTAL\r\n"
        );

        UART_WriteString(
            "================================\r\n\r\n"
        );


        /*
         * ----------------------------------------------------
         * LEER MCP9808
         * ----------------------------------------------------
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
         * ----------------------------------------------------
         * LEER VCNL4200
         * ----------------------------------------------------
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
         * ----------------------------------------------------
         * MOSTRAR TEMPERATURA
         * ----------------------------------------------------
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


        /*
         * ----------------------------------------------------
         * MOSTRAR LUZ
         * ----------------------------------------------------
         */

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
         * ----------------------------------------------------
         * ESTADO DE LOS SENSORES
         * ----------------------------------------------------
         */

        UART_WriteString(
            "\r\n--------------------------------\r\n"
        );

        UART_WriteString(
            "Actualizacion cada 3 segundos\r\n"
        );

        UART_WriteString(
            "--------------------------------\r\n"
        );


        /*
         * ----------------------------------------------------
         * ESPERAR 3 SEGUNDOS
         * ----------------------------------------------------
         */

        Delay_3_Seconds();
    }
}