/* Программа - термометр OneWire DS18B20+ с выводом на LCD WH0802 */
/* Отладочная плата MicroConverter SAR Eval Board Rev A3, Кварц 11,059 МГц */
/* 18 января 2021 */

#include <ADuC841.h> // Подключаем заголовочный файл для микроконтроллера ADuC841

//#include <intrins.h>  // Подлючаем файл со встрроенными в C51 функциями (нужен NOP)
//	_nop_() = 0.27 мкс

sbit DQ = P3 ^ 7; // Вывод DQ датчика DS18B20 с интерфейсом 1-wire
sbit RS = P2 ^ 5; // Вывод LCD выбора команды/символа. “1” – символ, “0”-команда
sbit RW = P2 ^ 6; // Вывод LCD чтения/записи. “0” – запись, “1” – чтение
sbit E = P2 ^ 7;  // Строб записи данных на дисплей

sbit OSCV = P3 ^ 3; // Вывод на осцилографф

sbit LED = P3 ^ 4; // Вывод на установленный на плате светодиод D1

char LCD_Print_Select = 0; // Переменная выбора того что выводить на дисплей

/* Время устнавливается в "тиках" срабатывания таймера, один такой "тик" = 5,93 милисекунд */

const unsigned int ButtonHoldTime = 67;  // Время программной защиты от дребезга контактов (~400 мс)
const unsigned int LCDTextRefresh = 170; // Время обновление данных на дисплее (мс)

int TicksHold = 0; // счетная переменная программной защиты от дребезга контактов
int TicksRefr = 0; // счетная переменная для обновления данных
bit Holdbit = 0;   // бит запрета при нажатии на кнопку

char OWRomCode[8];    // Массив байт для хранения 64-битного ROM кода устройства One-Wire
char OWScratchpad[9]; // Массив для считывания данных из Scratchpad устройства One-Wire

#define COMAND RS = 0;
#define RWDATA RS = 1;
#define WRITELCD RW = 0;
#define READLCD RW = 1;

/*  Переменные для значений количества тиков tickDelay для для 1-wire в соответствии с AN126 */
// A = 4, B = 42, C = 40, D = 7, E = 6, F = 37, G = 0, H = 315, I = 46, J = 269;

unsigned int z; // Внесение этой строки внутрь tickDelay уменьшает время выполнения tickDelay =)

void tickDelay(unsigned int delayTime) // Простое выжиадние времени
{
  // Необходимое время в микросекундах Tnom.
  // Количество тиков тогда (примерно) Nticks = Tnom/1.5

  for (z = 0; z < delayTime; z++)
    ;
}

/* ------------------------- One-Wire Interface Basic Functions -------------------------*/
// ----------------------------------------------------------------------------------------
void OW_WriteBit(bit CBIT) // Send a 1-Wire write bit. Provide 10us recovery time.
{
  if (CBIT == 1) // Write '1' bit
  {
    DQ = 0;        // Drives DQ low
    tickDelay(4);  // Timing 'A'
    DQ = 1;        // Releases the bus (DQ to high)
    tickDelay(42); // Timing 'B'
  }
  else
  {
    DQ = 0;        // Drives DQ low
    tickDelay(40); // Timing 'C'
    DQ = 1;        // Releases the bus (DQ to high)
    tickDelay(7);  // Timing 'D'
  }
}

int OW_ReadBit(void)
{
  int result;
  DQ = 0;       // Drives DQ low
  tickDelay(4); // Timing 'A'
  DQ = 1;       // Releases the bus (DQ to high)
  tickDelay(6); // Timing 'E'
  result = DQ;
  tickDelay(37); // Timing 'F'
  return result;
}

void OW_WriteByte(int datain)
{
  int loop;
  // Loop to write each bit in the byte, LS-bit first
  for (loop = 0; loop < 8; loop++)
  {
    OW_WriteBit(datain & 0x01);
    // shift the data byte for the next bit
    datain >>= 1;
  }
}

int OW_ReadByte(void)
{
  int loop, result = 0;
  for (loop = 0; loop < 8; loop++)
  {
    // shift the result to get it ready for the next bit
    result >>= 1;
    // if result is one, then set MS bit
    if (OW_ReadBit())
      result |= 0x80;
  }
  return result;
}

// Generate a 1-Wire reset, return 1 if no presence detect was found, return 0 otherwise.
int OW_TReset(void)
{
  int result;
  // tickDelay(G) _ G =0
  DQ = 0;         // Drives DQ low
  tickDelay(315); // Timing 'H' = 480 us
  DQ = 1;         // Releases the bus
  tickDelay(46);  // Timing 'I' = 70 us
  result = DQ;    // Sample for presence pulse from slave
  tickDelay(269); // Complete the reset sequence recovery
  return result;  // Return sample presence pulse result
}

void OW_Read_ROM_Code(void) // Send Command [33h] and Read 8 Bytes
{
  unsigned int Y;

  OW_WriteByte(0x33); // Read Rom [33h] Function Command

  for (Y = 0; Y < 8; Y++)
  {
    OWRomCode[Y] = OW_ReadByte();
  }
}

void OW_Match_ROM_Code(void) // Send Command [55h] and ROM
{
  unsigned int Y;
  OW_WriteByte(0x55); // Match Rom [55h] Function Command

  for (Y = 0; Y < 8; Y++)
  {
    OW_WriteByte(OWRomCode[Y]);
  }
}

void OW_Convert_T(void)
{
  OW_WriteByte(0x44); // Master issues Convert T command [44h]

  DQ = 1;
  LED = 1;

  while (DQ == 0)
  {
  }
  LED = 0;
}

void OW_ReadScratchpad(void) // Send Command [BEh] and Read 9 Bytes
{
  unsigned int i;
  OW_WriteByte(0xBE); // Master issues Read Scratchpad command [BEh]

  for (i = 0; i < 9; i++)
  {
    OWScratchpad[i] = OW_ReadByte(); // Read 9 Bytes - 0 to 8
  }
}

char OW_CRC_Byte(char InCRC, char InBit) // CRC 1-Wire  (Byte)
{
  char XorBit = 0x00;
  char NewCRC = 0x00;
  InBit &= 0x01;

  // CRC Algorithm with Polynomial = X8 + X5 + X4 + 1

  XorBit = ((InCRC & 0x01) ^ InBit) & 0x01;
  NewCRC |= ((XorBit << 7) & 0x80);                           // bit 8
  NewCRC |= (((InCRC & 0x80) >> 1) & 0x40);                   // bit 7
  NewCRC |= (((InCRC & 0x40) >> 1) & 0x20);                   // bit 6
  NewCRC |= (((InCRC & 0x20) >> 1) & 0x10);                   // bit 5
  NewCRC |= (((((InCRC & 0x10) >> 4) ^ XorBit) << 3) & 0x08); // bit 4
  NewCRC |= (((((InCRC & 0x08) >> 3) ^ XorBit) << 2) & 0x04); // bit 3
  NewCRC |= (((InCRC & 0x04) >> 1) & 0x02);                   // bit 2
  NewCRC |= (((InCRC & 0x02) >> 1) & 0x01);                   // bit 1

  return NewCRC;
}

char OW_CRC_ScPad(void) // CRC calculation for DS18B20 Scratchpad
{
  char ScpadCRC = 0x00;
  char Y;

  for (Y = 0; Y < 8; Y++)
  {
    char ZY;
    for (ZY = 0; ZY < 8; ZY++)
    {
      char OurBit = (OWScratchpad[Y] >> ZY) & 0x01;
      ScpadCRC = OW_CRC_Byte(ScpadCRC, OurBit);
    }
  }
  return ScpadCRC;
}

char OW_CRC_ROM(void) // CRC calculation for DS18B20 ROM (Serial Number)
{
  char RomCRC = 0x00;
  char Y;

  for (Y = 0; Y < 7; Y++)
  {
    char ZY;
    for (ZY = 0; ZY < 8; ZY++)
    {
      char OurBit = (OWRomCode[Y] >> ZY) & 0x01;
      RomCRC = OW_CRC_Byte(RomCRC, OurBit);
    }
  }

  return RomCRC;
}

void OW_ReadTemp(void)
{

  OW_TReset();
  OW_Match_ROM_Code();
  OW_Convert_T();
  OW_TReset();
  OW_Match_ROM_Code();
  OW_ReadScratchpad();
}

// ----------------------------------------------------------------------------------------

/* ------------------------------ LCD WH0802 Functions -----------------------------------*/

void SEND(unsigned char SDAT) // WH0802A Display Write Function with strobe
{

  P0 = SDAT; // WR DATA to Port 0;
  tickDelay(3);
  E = 1;
  tickDelay(3);
  E = 0;
  tickDelay(3);
  P0 &= ~0xFF;   // Reset Data Bits (P0 = 0000_0000)
  tickDelay(27); // Wait for more than 39us
}

char ConvertChar_LCD ( unsigned char InChar) 					// Convert Hex 0-F Number into WH0802A CODE
 {
		char OutChar = 0x00;
	 
	  
	  if (InChar < 0x01) InChar = 0x00;
   
		if (InChar == 0x00) OutChar = 0x30;
		if (InChar == 0x01) OutChar = 0x31;
	 	if (InChar == 0x02) OutChar = 0x32;
	 	if (InChar == 0x03) OutChar = 0x33;
	 	if (InChar == 0x04) OutChar = 0x34;
	 	if (InChar == 0x05) OutChar = 0x35;
	 	if (InChar == 0x06) OutChar = 0x36;
	 	if (InChar == 0x07) OutChar = 0x37;
	 	if (InChar == 0x08) OutChar = 0x38;
	 	if (InChar == 0x09) OutChar = 0x39;
   
	 	if (InChar == 0x0A) OutChar = 0x41;
	 	if (InChar == 0x0B) OutChar = 0x42;
	 	if (InChar == 0x0C) OutChar = 0x43;
	 	if (InChar == 0x0D) OutChar = 0x44;
	 	if (InChar == 0x0E) OutChar = 0x45;
	 	if (InChar == 0x0F) OutChar = 0x46;   
		
    if (InChar > 0x0F) OutChar = 0x3F;
    
	 return OutChar;
}

void InitializingLCD_8bit(void)
{

  tickDelay(0x7530); // Wait for more than 40 ms after VDDrises to 4.5 V
  SEND(0x38);        // Function set, 8bit, 2-line
  tickDelay(27);     // Wait for more than 39us
  SEND(0x38);        // Function set, 8bit, 2-line
  tickDelay(27);     // Wait for more than 39us
  SEND(0x0C);        // Display Set (No Cursor & No Blinking)
  tickDelay(34);     // Wait for more than 50 us
  SEND(0x01);        // Display Clear
  tickDelay(1020);   // Wait for more than 1.53ms
  SEND(0x06);        // Entery Mode Set (Right-Moving Cursor)
  tickDelay(34);     // Wait for more than 50 us
}

void LCD_Print_OneWire_NA(void)
{
  COMAND;
  SEND(0x80); // First Line
  RWDATA;     //
  SEND(0x44);
  SEND(0x53);
  SEND(0x31);
  SEND(0x38);
  SEND(0x42);
  SEND(0x32);
  SEND(0x30);
  SEND(0x2B);
  COMAND;
  SEND(0xC0); // 2-nd line
  RWDATA;
  SEND(0x48);
  SEND(0x45);
  SEND(0x20);
  SEND(0xA8);
  SEND(0x4F);
  SEND(0xE0);
  SEND(0x4B);
  SEND(0xA7);
}

void HEXDEC(unsigned int HEXfra)

{
  unsigned int dectemp = 1000;
  unsigned int hextemp = HEXfra;
  unsigned int zz = 0;

  char TTEMP[4] = {0, 0, 0, 0};

  for (zz = 0; zz < 4; zz++)
  {
    TTEMP[zz] = (hextemp / dectemp);
    hextemp = hextemp - (TTEMP[zz] * dectemp);
    dectemp = dectemp / 10;
    SEND(ConvertChar_LCD(TTEMP[zz]));
  }
}

void LCD_Print_Temp(void)
{
  unsigned char Te; // "Te" - целая часть значения температуры, максимум +125
  unsigned int Tf;  // "Tf" - дробная часть значения температуры
  char SignT;       //  Знак температуры в кодировке дисплея

  OW_ReadTemp(); // Читаем Scratchpad из датчика DS18B20+

  if (OWScratchpad[8] == OW_CRC_ScPad()) // Проверяем CRC -> сравниваем значение из датчика с расчетным
  {                                      // Если значения CRC совпадают, то выполняем код ниже

    Te = ((OWScratchpad[1] & 0x07) << 4) | ((OWScratchpad[0] >> 4) & 0x0F); //собираем из частей двух
    Tf = (OWScratchpad[0] & 0x0F) * 625;                                    // Дробная часть 0,625 х 4 бита (0,625 * 15 = 0,5 градуса)

    // Если температура имеет не отрицательное значение
    if ((OWScratchpad[1] >> 4) == 0x00)
    {
      // Если температура ровно "0"
      if ((OWScratchpad[0] == 0) & (OWScratchpad[1]) == 0)
      {
        SignT = 0x20; //  Первый символ " " т.е. пробел
      }
      else
      {
        SignT = 0x2B;
      } // Первый символ "+"
    }
    else // Если температура имеет отрицательное значение
    {
      SignT = 0x2D; // Первый символ "-"
      Te = ~Te;     // Инвертируем значение температуры
      Tf = ~(Tf - 0x01);
    }

    COMAND;
    SEND(0x01);      // Display Clear
    tickDelay(1030); // Wait for more than 1.53ms

    COMAND;
    SEND(0x80); // First Line  - Temp
    RWDATA;

    SEND(SignT);

    if ((Te / 100) > 0)
      SEND(ConvertChar_LCD(Te / 100));
    SEND(ConvertChar_LCD((Te % 100) / 10));
    SEND(ConvertChar_LCD((Te % 100) % 10));
    SEND(0x2E);
    SEND(ConvertChar_LCD(Tf / 1000));
    if ((Te / 100) == 0)
      SEND(ConvertChar_LCD((Tf % 1000) / 100));
    SEND(0xEF);
    SEND(0x43);

    COMAND;
    SEND(0xC0); // 2-nd line - "DS18B20+"
    RWDATA;

    SEND(0x44);
    SEND(0x53);
    SEND(0x31);
    SEND(0x38);
    SEND(0x42);
    SEND(0x32);
    SEND(0x30);
    SEND(0x2B);
  }
  else
  {
    COMAND;
    SEND(0x80);
    RWDATA; // First Line "DS18B20+"
    SEND(0x44);
    SEND(0x53);
    SEND(0x31);
    SEND(0x38);
    SEND(0x42);
    SEND(0x32);
    SEND(0x30);
    SEND(0x2B);
    COMAND;
    SEND(0xC0);
    RWDATA; // Second Line "CRC ERR."
    SEND(0x43);
    SEND(0x52);
    SEND(0x43);
    SEND(0x20);
    SEND(0x45);
    SEND(0x52);
    SEND(0x52);
    SEND(0x2E);
  }
}

void LCD_Print_RomCode(void)
{

  unsigned int Y;

  //OW_Read_ROM_Code();

  COMAND;
  SEND(0x01);      // Display Clear
  tickDelay(1030); // Wait for more than 1.53ms

  COMAND;
  SEND(0x80); // First Line
  RWDATA;

  for (Y = 0; Y < 8; Y++)
  {
    if (Y == 4)
    {
      COMAND;
      SEND(0xC0);
      RWDATA;
    }
    SEND(ConvertChar_LCD((OWRomCode[Y] >> 4) & 0x0F));
    SEND(ConvertChar_LCD(OWRomCode[Y] & 0x0F));
  }
}

void LCD_Print_ScrPad8(void)
{

  unsigned int Y;

  OW_ReadTemp(); // Читаем Scratchpad из датчика DS18B20+

  COMAND;
  SEND(0x01);      // Display Clear
  tickDelay(1030); // Wait for more than 1.53ms

  COMAND;
  SEND(0x80); // First Line
  RWDATA;

  for (Y = 0; Y < 8; Y++)
  {
    if (Y == 4)
    {
      COMAND;
      SEND(0xC0);
      RWDATA;
    }
    SEND(ConvertChar_LCD((OWScratchpad[Y] >> 4) & 0x0F));
    SEND(ConvertChar_LCD(OWScratchpad[Y] & 0x0F));
  }
}

void LCD_Print_Selected_One(void)
{

  switch (LCD_Print_Select)
  {
  case 0:
  {
    LCD_Print_Temp();
    break;
  }

  case 1:
  {
    LCD_Print_ScrPad8();
    break;
  }

  case 2:
  {
    LCD_Print_RomCode();
    break;
  }
  }
}

void int0(void) interrupt 0 // Отработка прерывания по нажатию кнопки (переход вывода INT0 c лог.1 на лог.0)
{

  /* Программная реализация защиты от дребезга контактов по кнопке INT0 */

  if (!Holdbit)
  {

    TL0 = 0;
    TH0 = 0; // Начальное счетное значение таймера 0
    TR0 = 1; // Запускаем Таймер 0
    Holdbit = 1;

    LCD_Print_Select++;
    if (LCD_Print_Select > 2)
      LCD_Print_Select = 0;

    LCD_Print_Selected_One();
  }
}

void T0Isr(void) interrupt 1 // Отработка прерывания от Таймера 0
{
  TF0 = 0; // Сброс флага прерывания таймера
  //TL0 = 0; TH0 = 0;     // Начальное счетное значение таймера 0

  if (TicksHold < ButtonHoldTime)
  {
    TicksHold++; // счетная переменная увеличивается на 1

    TR0 = 1; // Запускаем Таймер 0 вновь
  }
  else
  {
    TR0 = 0;
    TicksHold = 0; // Обнуляем переменную
    Holdbit = 0;
  }
}

void T2Isr(void) interrupt 5 using 1 // Отработка прерывания от Таймера 1
{
  TF2 = 0; // Сброс флага прерывания таймера

  if (TicksRefr < LCDTextRefresh)
  {
    TicksRefr++; // счетная переменная увеличивается на 1
  }
  else
  {
    TicksRefr = 0; // Обнуляем переменную

    LCD_Print_Selected_One(); // обновляем данные на дисплее

    OSCV = ~OSCV;
  }
}

void main(void) //Основная программа
{
  int times = 0;

  RS = 0;
  RW = 0;
  E = 0;
  P0 = 0x00; // Обнуляем выводы LCD

  EA = 1;   // Разрешение работы системы прерываний
  INT0 = 1; // Переводим вывод P3.2 в альтернативную функцию - вход прерывания INT0
  EX0 = 1;  // Разрешение прерывания по входу INT0 (по нажатию кнопки)
  IT0 = 1;  // Прерывание INT0 инициализируется перепадом с лог.1 на лог.0
  PX0 = 1;  // Установка высшего приоритета для прерывания по INT0 (нажатие кнопки)
  ET0 = 1;  // Разрешение прерывания для Таймер 0

  TMOD |= 0x1; // Таймеры 0 устанавливаем в режим 16-ти разрядного таймера

  DQ = 1; // Поддтягиваем Шину данных 1-wire к +5В (Pull-up резистор)

  InitializingLCD_8bit();

  if (OW_TReset() & 0x01) // Если устройство One-Wire не подключено, то на экран выводится
  {                       // Сообщение: DS18B20+ НЕ ПОДКЛ
    LCD_Print_OneWire_NA();
  }
  else // устройство 1-Wire ответило "Presence"
  {
    OW_Read_ROM_Code(); // читаем Серийный номер устройства

    /* Если код устройтсва OW_FamilyCode = 028h и расчет CRC верен то Зажигаем светодиод (LED=0) */
    if ((OWRomCode[0] == 0x28) & (OWRomCode[7] == OW_CRC_ROM()))

    {
      LED = 0; // Зажигаем светодиод (LED=0)

      //OW_ReadTemp();
      //LCD_Print_Temp();
    }
  }

  /*
OSCV = 1;

RCAP2H = 0x0;
RCAP2L = 0x0;
T2CON &= 0x0FC;
ET2 = 1;

T2CON |= 0x4;

*/

  LCD_Print_Selected_One();

  while (1) /* Весьма Бесконечный цикл */
  {

    times++;
    if (times == 4320)
    {
      times = 0;
      TicksRefr++;
      if (TicksRefr == LCDTextRefresh)
      {
        TicksRefr = 0;
        OSCV = ~OSCV;
        LCD_Print_Selected_One();
      }
    }
  }

} // END OF FILE
