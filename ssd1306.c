#include <xc.h>
#include <stdint.h>
#include "ssd1306.h"
#include "font.h"

/*
 * =========================================================
 * FRECUENCIA DEL OSCILADOR
 * =========================================================
 *
 * MCC ya genera _XTAL_FREQ = 32000000U en clock.h.
 * Lo definimos aquí solamente si todavía no existe.
 */
#ifndef _XTAL_FREQ
#define _XTAL_FREQ 32000000UL
#endif


/*
 * =========================================================
 * SSD1306 INITIALIZATION
 * =========================================================
 */
void SSD1306_Init(void)
{
    __delay_ms(200);

    /* Display OFF */
    SSD1306_SendCommand(SSD1306_DISPLAY_OFF);

    /* Display clock */
    SSD1306_SendCommand(SSD1306_SET_DISPLAY_CLOCK_DIV_RATIO);
    SSD1306_SendCommand(0x80);

    /* Multiplex ratio: 64 líneas */
    SSD1306_SendCommand(SSD1306_SET_MULTIPLEX_RATIO);
    SSD1306_SendCommand(SSD1306_LCDHEIGHT - 1);

    /* Display offset */
    SSD1306_SendCommand(SSD1306_SET_DISPLAY_OFFSET);
    SSD1306_SendCommand(0x00);

    /* Start line = 0 */
    SSD1306_SendCommand(SSD1306_SET_START_LINE | 0x00);

    /* Charge pump */
    SSD1306_SendCommand(SSD1306_CHARGE_PUMP);
    SSD1306_SendCommand(0x14);

    /* Horizontal addressing mode */
    SSD1306_SendCommand(SSD1306_MEMORY_ADDR_MODE);
    SSD1306_SendCommand(0x00);

    /* Segment remap */
    SSD1306_SendCommand(SSD1306_SET_SEGMENT_REMAP | 0x01);

    /* COM scan direction */
    SSD1306_SendCommand(SSD1306_COM_SCAN_DIR_DEC);

    /* COM pins */
    SSD1306_SendCommand(SSD1306_SET_COM_PINS);
    SSD1306_SendCommand(0x12);

    /* Contrast */
    SSD1306_SendCommand(SSD1306_SET_CONTRAST_CONTROL);
    SSD1306_SendCommand(0x8F);

    /* Pre-charge */
    SSD1306_SendCommand(SSD1306_SET_PRECHARGE_PERIOD);
    SSD1306_SendCommand(0xF1);

    /* VCOMH */
    SSD1306_SendCommand(SSD1306_SET_VCOM_DESELECT);
    SSD1306_SendCommand(0x40);

    /* Resume RAM */
    SSD1306_SendCommand(SSD1306_DISPLAY_ALL_ON_RESUME);

    /* Normal display */
    SSD1306_SendCommand(SSD1306_NORMAL_DISPLAY);

    /* Disable scrolling */
    SSD1306_SendCommand(SSD1306_DEACTIVATE_SCROLL);

    /* Display ON */
    SSD1306_SendCommand(SSD1306_DISPLAY_ON);

    __delay_ms(100);
}


/*
 * =========================================================
 * SEND COMMAND
 * =========================================================
 */
void SSD1306_SendCommand(uint8_t command)
{
    uint8_t cmd[2];

    cmd[0] = SSD1306_COMMAND;
    cmd[1] = command;

    I2C1_Write(
        SSD1306_I2C_ADDRESS,
        cmd,
        2
    );

    __delay_us(100);
}


/*
 * =========================================================
 * SEND DATA
 * =========================================================
 */
void SSD1306_SendData(uint8_t data)
{
    uint8_t d[2];

    d[0] = SSD1306_DATA_CONTINUE;
    d[1] = data;

    I2C1_Write(
        SSD1306_I2C_ADDRESS,
        d,
        2
    );

    __delay_us(100);
}


/*
 * =========================================================
 * WRITE STRING
 * =========================================================
 */
void SSD1306_WriteString(char *characters)
{
    if (characters == NULL)
    {
        return;
    }

    while (*characters != '\0')
    {
        SSD1306_WriteCharacter(*characters);
        characters++;
    }
}


/*
 * =========================================================
 * WRITE CHARACTER
 * =========================================================
 */
void SSD1306_WriteCharacter(char character)
{
    uint8_t i;
    uint8_t index;

    /*
     * Tabla ASCII:
     *
     * 0x20 = espacio
     * 0x7F = último carácter
     */
    if ((uint8_t)character < 0x20 ||
        (uint8_t)character > 0x7F)
    {
        return;
    }

    index = (uint8_t)character - 0x20;

    for (i = 0; i < 5; i++)
    {
        SSD1306_SendData(
            ASCII[index][i]
        );
    }

    /* Espacio entre caracteres */
    SSD1306_SendData(0x00);
}


/*
 * =========================================================
 * CLEAR DISPLAY
 * =========================================================
 */
void SSD1306_Clear(void)
{
    uint16_t i;

    SSD1306_SelectPage(0);

    for (i = 0; i < SSD1306_CLEAR_SIZE; i++)
    {
        SSD1306_SendData(0x00);
    }
}


/*
 * =========================================================
 * CLEAR ONE PAGE
 * =========================================================
 */
void SSD1306_ClearLine(uint8_t page_num)
{
    uint8_t i;

    if (page_num > 7)
    {
        return;
    }

    SSD1306_SelectPage(page_num);

    for (i = 0; i < 128; i++)
    {
        SSD1306_SendData(0x00);
    }
}


/*
 * =========================================================
 * SELECT PAGE
 * =========================================================
 */
void SSD1306_SelectPage(uint8_t page_num)
{
    uint8_t result;

    if (page_num > 7)
    {
        return;
    }

    /*
     * Seleccionar página
     */
    result = 0xB0 | page_num;

    SSD1306_SendCommand(result);

    /*
     * Columna baja = 0
     */
    SSD1306_SendCommand(
        SSD1306_SET_LOWER_COLUMN
    );

    /*
     * Columna alta = 0
     */
    SSD1306_SendCommand(
        SSD1306_SET_HIGHER_COLUMN
    );
}


/*
 * =========================================================
 * DRAW BITMAP
 * =========================================================
 */
void SSD1306_DrawBitmap(const uint8_t *bitmap)
{
    uint16_t byteIndex = 0;
    uint8_t row;
    uint8_t col;

    if (bitmap == NULL)
    {
        return;
    }

    /*
     * 8 páginas x 128 columnas
     * = 1024 bytes
     */
    for (row = 0; row < 8; row++)
    {
        for (col = 0; col < 128; col++)
        {
            SSD1306_SendData(
                bitmap[byteIndex]
            );

            byteIndex++;
        }
    }

    __delay_ms(50);
}