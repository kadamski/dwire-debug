/// DebugPort.c

#define ByteArrayLiteral(...) (u8[]){__VA_ARGS__}, sizeof((u8[]){__VA_ARGS__})








void ConnectSerialPort();

void DwSerialWrite(const u8 *bytes, int length) {
  if (!SerialPort) {ConnectSerialPort();}
  SerialWrite(bytes, length);
}
#define SerialWrite "Don't use SerialWrite, use DwSerialWrite"

void DwSerialRead(u8 *buf, int len) {
  if (!SerialPort) {ConnectSerialPort();}
  SerialRead(buf, len);
}
#define SerialRead "Don't use SerialRead, use DwSerialRead"







/// DebugWire port access

void DwExpect(const u8 *bytes, int len) {
  u8 actual[len];
  DwSerialRead(actual, len);
  for (int i=0; i<len; i++) {
    if (actual[i] != bytes[i]) {
      Ws("WriteDebug, byte "); Wd(i+1,1); Ws(" of "); Wd(len,1);
      Ws(": Read "); Wx(actual[i],2); Ws(" expected "); Wx(bytes[i],2); Wl(); Fail("");
    }
  }
}


void DwWrite(const u8 *bytes, int len) {
  DwSerialWrite(bytes, len);
  DwExpect(bytes, len);
}


u8 hi(int w) {return (w>>8)&0xff;}
u8 lo(int w) {return (w   )&0xff;}


void DwWriteWord(int word) {
  DwWrite(ByteArrayLiteral(hi(word), lo(word)));
}


int DwReadByte() {u8 byte   = 0;   DwSerialRead(&byte, 1); return byte;}
int DwReadWord() {u8 buf[2] = {0}; DwSerialRead(buf, 2);   return (buf[0] << 8) | buf[1];}




void DwSync() { // Used after reset/go/break
  u8 byte = 0;
  while ((byte = DwReadByte()) == 0x00) {/*Ws("0 "); Flush();*/}     // Eat 0x00 bytes generated by line at break (0v)
  while (byte == 0xFF) {/*Ws("FF "); Flush();*/ byte = DwReadByte();}  // Eat 0xFF bytes generated by line going high following break.
  if (byte != 0x55) {Wsl("Didn't receive 0x55 on reconnection, got "); Wx(byte,2); Fail("");}
}


void DwBreak() {SerialBreak(400); DwSync();}


void CheckDevice() {if (DeviceType<0) {Fail("Device not recognised.");}}
int  IoregSize()   {CheckDevice(); return Characteristics[DeviceType].ioregSize;}
int  SramSize()    {CheckDevice(); return Characteristics[DeviceType].sramSize;}
int  EepromSize()  {CheckDevice(); return Characteristics[DeviceType].eepromSize;}
int  FlashSize()   {CheckDevice(); return Characteristics[DeviceType].flashSize;}   // In bytes
int  PageSize()    {CheckDevice(); return Characteristics[DeviceType].pageSize;}    // In bytes
int  DWDRreg()     {CheckDevice(); return Characteristics[DeviceType].DWDR;}
int  DWDRaddr()    {CheckDevice(); return Characteristics[DeviceType].DWDR + 0x20;} // IO regs come after the 32 regs r0-r31
int  DataLimit()   {CheckDevice(); return 32 + IoregSize() + SramSize();}

void SetSizes(int signature) {
  int i=0; while (Characteristics[i].signature  &&  Characteristics[i].signature != signature) {i++;}

  if (Characteristics[i].signature) {
    DeviceType = i;
    Ws(Characteristics[DeviceType].name); Ws(" found on "); Wsl(UsbSerialPortName);
  } else {
    DeviceType = -1;
    Ws("Unrecognised device signature: "); Wx(signature,4); Fail("");
  }
}




void DwReadRegisters(u8 *registers, int first, int count) {
  //wsl("Read Registers.");
  // Read Registers (destroys PC and BP)
  DwWrite(ByteArrayLiteral(
    0x66,                 // Access registers/memory mode
    0xD0, 0, first,       // Set PC = first
    0xD1, 0, first+count, // Set BP = limit
    0xC2, 1, 0x20         // Start register read
  ));
  DwSerialRead(registers, count);
}


void DwWriteRegisters(u8 *registers, int first, int count) {
  //wsl("Write Registers.");
  // Write Registers (destroys PC and BP)
  DwWrite(ByteArrayLiteral(
    0x66,                 // Access registers/memory mode
    0xD0, 0, first,       // Set PC = first
    0xD1, 0, first+count, // Set BP = limit
    0xC2, 5, 0x20         // Start register write
  ));
  DwWrite(registers, count);
}


void DwUnsafeReadAddr(int addr, int len, u8 *buf) {
  // Do not read addresses 30, 31 or DWDR as these interfere with the read process
  DwWrite(ByteArrayLiteral(
    0x66, 0xD0, 0,0x1e, 0xD1, 0,0x20,       // Set PC=0x001E, BP=0x0020 (i.e. address register Z)
    0xC2, 5, 0x20, lo(addr), hi(addr),      // Write SRAM address of first IO register to Z
    0xD0, 0,0, 0xD1, hi(len*2), lo(len*2),  // Set PC=0, BP=2*length
    0xC2, 0, 0x20                           // Start the read
  ));
  DwSerialRead(buf, len);
}

void DwReadAddr(int addr, int len, u8 *buf) {
  // Read range before r30
  int len1 = min(len, 30-addr);
  if (len1 > 0) {DwUnsafeReadAddr(addr, len1, buf); addr+=len1; len-=len1; buf+=len1;}

  // Registers 30 and 31 are cached - use the cached values.
  if (addr == 30  &&  len > 0) {buf[0] = R30; addr++; len--; buf++;}
  if (addr == 31  &&  len > 0) {buf[0] = R31; addr++; len--; buf++;}

  // Read range from 32 to DWDR
  int len2 = min(len, DWDRaddr()-addr);
  if (len2 > 0) {DwUnsafeReadAddr(addr, len2, buf); addr+=len2; len-=len2; buf+=len2;}

  // Provide dummy 0 value for DWDR
  if (addr == DWDRaddr()  &&  len > 0) {buf[0] = 0; addr++; len--; buf++;}

  // Read anything beyond DWDR
  if (len > 0) {DwUnsafeReadAddr(addr, len, buf);}
}


void DwUnsafeWriteAddr(int addr, int len, const u8 *buf) {
  // Do not write addresses 30, 31 or DWDR as these interfere with the write process
  DwWrite(ByteArrayLiteral(
    0x66, 0xD0, 0,0x1e, 0xD1, 0,0x20,           // Set PC=0x001E, BP=0x0020 (i.e. address register Z)
    0xC2, 5, 0x20, lo(addr), hi(addr),          // Write SRAM address of first IO register to Z
    0xD0, 0,1, 0xD1, hi(len*2+1), lo(len*2+1),  // Set PC=0, BP=2*length
    0xC2, 4, 0x20                               // Start the write
  ));
  DwWrite(buf, len);
}

void DwWriteAddr(int addr, int len, const u8 *buf) {
  // Write range before r30
  int len1 = min(len, 30-addr);
  if (len1 > 0) {DwUnsafeWriteAddr(addr, len1, buf); addr+=len1; len-=len1; buf+=len1;}

  // Registers 30 and 31 are cached - update the cached values.
  if (addr == 30  &&  len > 0) {R30 = buf[0]; addr++; len--; buf++;}
  if (addr == 31  &&  len > 0) {R31 = buf[0]; addr++; len--; buf++;}

  // Write range from 32 to DWDR
  int len2 = min(len, DWDRaddr()-addr);
  if (len2 > 0) {DwUnsafeWriteAddr(addr, len2, buf); addr+=len2; len-=len2; buf+=len2;}

  // (Ignore anything for DWDR - as the DebugWIRE port it is in use and wouldn't retain a value anyway)
  if (addr == DWDRaddr()  &&  len > 0) {addr++; len--; buf++;}

  // Write anything beyond DWDR
  if (len > 0) {DwUnsafeWriteAddr(addr, len, buf);}
}


void DwReadFlash(int addr, int len, u8 *buf) {
  int limit = addr + len;
  if (limit > FlashSize()) {Fail("Attempt to read beyond end of flash.");}
  while (addr < limit) {
    int length = min(limit-addr, 256); // Read no more than 256 bytes at a time.
    //Ws("ReadFlashBlock at $"); Wx(addr,1); Ws(", length $"); Wx(length,1); Wl();
    DwWrite(ByteArrayLiteral(
      0x66, 0xD0, 0,0x1e, 0xD1, 0,0x20,           // Set PC=0x001E, BP=0x0020 (i.e. address register Z)
      0xC2, 5, 0x20, lo(addr),hi(addr),           // Write addr to Z
      0xD0, 0,0, 0xD1, hi(2*length),lo(2*length), // Set PC=0, BP=2*len
      0xC2, 2, 0x20                               // Read length bytes from flash starting at first
    ));
    DwSerialRead(buf, length);
    addr += length;
    buf  += length;
  }
}


void DwReconnect() {
  DwWrite(ByteArrayLiteral(0xF0)); PC = DwReadWord()-1;
  u8 buf[2];
  DwReadRegisters(buf, 30, 2);
  R30 = buf[0];
  R31 = buf[1];
}

void DwConnect() {
  DwReconnect();
  DwWrite(ByteArrayLiteral(0xF3)); SetSizes(DwReadWord());
}

void DwReset() {
  DwBreak();
  DwWrite(ByteArrayLiteral(7)); DwSync();
  DwReconnect();
}


void DwTrace() { // Execute one instruction
  DwWrite(ByteArrayLiteral(
    0x66,                          // Register or memory access mode
    0xD0, 0, 30,                   // Set up to set registers starting from r30
    0xD1, 0, 32,                   // Register limit is 32 (i.e. stop at r31)
    0xC2, 5, 0x20,                 // Select reigster write mode and start
    R30, R31,                      // Cached value of r30 and r31
    0x60, 0xD0, hi(PC), lo(PC),    // Address to restart execution at
    0x31                           // Continue execution (single step)
  ));

  //SerialDump();

  DwSync(); DwReconnect();
}


void DwGo() { // Begin executing.
  DwWrite(ByteArrayLiteral(
    0x66,                          // Register or memory access mode
    0xD0, 0, 30,                   // Set up to set registers starting from r30
    0xD1, 0, 32,                   // Register limit is 32 (i.e. stop at r31)
    0xC2, 5, 0x20,                 // Select reigster write mode and start
    R30, R31                       // Cached value of r30 and r31
  ));

  if (BP < 0) DwWrite(ByteArrayLiteral(
    0x60, 0xD0, hi(PC), lo(PC),    // Address to restart execution at
    0x30                           // Continue execution (go)
  )); else DwWrite(ByteArrayLiteral(
    0xD1, hi(BP), lo(BP),          // Set breakpoint for execution to stop at
    0x61, 0xD0, hi(PC), lo(PC),    // Address to restart execution at (with BP enable)
    0x30                           // Continue execution (go)
  ));
}


#if 0

u16 GetPC()  {DwByte(0xF0); return DwReadWord();}
u16 GetBP()  {DwByte(0xF1); return DwReadWord();}
u16 GetIR()  {DwByte(0xF2); return DwReadWord();}
u16 GetSR()  {DwByte(0xF3); return DwReadWord();}

u16 PC = 0;
u16 BP = 0;
u16 IR = 0;
u16 SR = 0;



void ShowControlRegisters() {
  ws("Control registers: PC "); wx(PC,4);
  ws(", BP ");       wx(BP,4);
  ws(", IR ");       wx(IR,4);
  ws(", SR ");       wx(SR,4);
  wl();
}

#ifdef windows
  void __chkstk_ms(void) {}
#endif






// Data memory
//
// The data array tracks the AtTiny45 data memory:
//    Data[$00-$01F]: 32 bytes of r0-r31
//    Data[$20-$05F]: 64 bytes of I/O registers
//    Data[$60-$15F]: 256 bytes of static RAM


u8 DataMemory[32+64+256] = {};  // 32 bytes r0-r31, 64 bytes of IO registers, 256 bytes of RAM.
int RamLoaded = 0;

enum { // IO register address in data memory
  DdrB  = 0x37,
  PortB = 0x38,
  Dwdr  = 0x42, // (Inaccesible)
  SpL   = 0x5D,
  SpH   = 0x5E,
  Sreg  = 0x5F
};



void LoadRAM() {
  u8 cmd[] = {
    0xD0, 0,0x1e, 0xD1, 0,0x20,  // Set PC=0x001E, BP=0x0020 (i.e. address register Z)
    0xC2, 5, 0x20, 0x60,0,       // Set Z to address of start of SRAM in data memory
    0xD0, 0,0, 0xD1, 2,0,        // Set PC=0, BP=2*length
    0xC2, 0, 0x20                // Read 256 bytes from memory
  };
  DwWrite(cmd, sizeof cmd);
  SerialReadBytes(&DataMemory[0x60], 256);
  RamLoaded = 1;
}


void StoreRegisters() {
  u8 cmd[] = {
    0x66,                   // Access registers/memory mode
    0xD0, 0,0, 0xD1, 0,32,  // Set PC=0, BP=32 (i.e. address all registers)
    0xC2, 5, 0x20           // Start register write
  };
  DwWrite(cmd, sizeof(cmd));
  DwWrite(DataMemory, 32);
}


//void StoreRAM() {
//  u8 cmd[] = {
//    0xD0, 0,0x1e, 0xD1, 0,0x20,  // Set PC=0x001E, BP=0x0020 (i.e. address register Z)
//    0xC2, 5, 0x20, 0x60,0,       // Set Z to address of start of SRAM in data memory
//    0xD0, 0,0, 0xD1, 2,0,        // Set PC=0, BP=2*length
//    0xC2, 4, 0x20                // Write 256 bytes to memory
//  };
//  DwWrite(cmd, sizeof cmd);
//  SerialWriteBytes(&DataMemory[0x60], 256);
//}



void ReadFlash(const u8 *buf, u16 first, u16 length) {
  u8 cmd[] = {
    0xD0, 0,0x1e, 0xD1, 0,0x20,                  // Set PC=0x001E, BP=0x0020 (i.e. address register Z)
    0xC2, 5, 0x20, lo(first),hi(first),          // Write first to Z
    0xD0, 0,0, 0xD1, hi(2*length),lo(2*length),  // Set PC=0, BP=2*length
    0xC2, 2, 0x20                                // Read length bytes from flash starting at first
  };
  DwWrite(cmd, sizeof cmd);
  SerialReadBytes(buf, length);
}

enum {FlashSize=4096};
u8 FlashBytes[FlashSize] = {};

void LoadAllFlash() {
  int chunksize = 256;
  for (int i=0; i<sizeof(FlashBytes)/chunksize; i++) {
    ReadFlash(&FlashBytes[i*chunksize], i*chunksize, chunksize);
  }
}

void LoadFlashBytes(u16 first, u16 limit) {
  u16 length = limit-first;
  u8 cmd[] = {
    0xD0, 0,0x1e, 0xD1, 0,0x20,                  // Set PC=0x001E, BP=0x0020 (i.e. address register Z)
    0xC2, 5, 0x20, lo(first),hi(first),          // Write first to Z
    0xD0, 0,0, 0xD1, hi(2*length),lo(2*length),  // Set PC=0, BP=2*length
    0xC2, 2, 0x20                                // Read length bytes from flash starting at first
  };
  DwWrite(cmd, sizeof cmd);
  SerialReadBytes(&FlashBytes[first], length);
}

u16 FlashWord(u16 addr) {
  return FlashBytes[2*addr] + (FlashBytes[2*addr + 1] << 8);
}


#endif


//void ReadFlash(u16 first, u16 limit) {
//  SetRegisterPair(0x1e, first);  // Write start address to register pair R30/R31 (aka Z)
//  // Read Flash starting at Z
//  SetPC(0); SetBP(2*(limit-first)); SetMode(2); DwGo();
//  for (int i=first; i<limit; i++) {wDumpByte(i, DwSerialRead());}
//  wDumpEnd();
//  RestoreControlRegisters();
//}


//void SetSRAMByte(u16 addr, u8 value) {
//  SetRegisterPair(0x1e, addr);
//  SetPC(1); SetBP(3); SetMode(4); DwGo();
//  DwByte(value);
//  RestoreControlRegisters();
//}


/// DebugWire protocol




/*

DebugWire command byte interpretation:

06      00 xx x1 10   Disable dW (Enable ISP programming)
07      00 xx x1 11   Reset

20      00 10 00 00   go start reading/writing SRAM/Flash based on low byte of IR
21      00 10 00 01   go read/write a single register
23      00 10 00 11   execute IR (single word instruction loaded with D2)

30      00 11 00 00   go normal execution
31      00 11 00 01   single step (Rikusw says PC increments twice?)
32      00 11 00 10   go using loaded instruction
33      00 11 00 11   single step using slow loaded instruction (specifically spm)
                      will generate break and 0x55 output when complete.

t: disable timers
40/60   01 t0 00 00   Set GO context  (No bp?)
41/61   01 t0 00 01   Set run to cursor context (Run to hardware BP?)
43/63   01 t0 00 11   Set step out context (Run to return instruction?)
44/64   01 t0 01 00   Set up for single step using loaded instruction
46/66   01 t0 01 10   Set up for read/write using repeating simulated instructions
59/79   01 t1 10 01   Set step-in / autostep context or when resuming a sw bp (Execute a single instruction?)
5A/7A   01 t1 10 10   Set single step context



83      10 d0 xx dd   Clock div

C2      11 00 00 10   Set read/write mode (folowed by SRAM 0, Regs 1, Flash 2)

w:  word operation (low byte only if 0)
cc: control regs: 0: PC, 1: BP, 2: IR, 3: Sig.
Cx/Dx   11 0w xx cc   Set control reg
Ex/Fx   11 1w xx cc   Read control reg


Modes:

0 (SRAM) repeating instructions:

ld  r16,Z+       or     in r16,DWDR
out DWDR,r16     or     st Z+,r16

1 (Regs) repeating instructions
out DWDR,r0      or     in r0,DWDR
out DWDR,r1      or     in r1,DWDR
out DWDR,r2      or     in r2,DWDR
...                     ....

2 (Flash) repeating instructions

lpm r?,Z+        or    ?unused
out SWDR,r?      or    ?unused



-------------------------------------------------------------------------



40/60   0   0   0   0   0   GO                         Set GO context  (No bp?)
41/61   0   0   0   0   1   Run to cursor              Set run to cursor context (Run to hardware BP?)
43/63   0   0   0   1   1   Step out                   Set step out context (Run to return instruction?)
44/64   0   0   1   0   0   Write flash page           Set up for single step using loaded instruction
46/66   0   0   1   1   0   Use virtual instructions   Set up for read/write using repeating simulated instructions
59/79   1   1   0   0   1   Step in/autostep           Set step-in / autostep context or when resuming a sw bp (Execute a single instruction?)
5A/7A   1   1   0   1   0   Single step                Set single step context
        ^   ^   ^   ^   ^
        |   |   |   '---'------ 00 no break, 01 Break at BP, 10 ?, 11 ? break at return?
        |   |   '-------------- PC represents Flash (0) or virtual space (1)
        '---'------------------




20      0 0 0   go start reading/writing SRAM/Flash based on low byte of PC
21      0 0 1   single step read/write a single register
23      0 1 1   single step an instruction loaded with D2
30      1 0 0   go normal execution
31      1 0 1   single step (Rikusw says PC increments twice?)
32      1 1 0   go using loaded instruction
33      1 1 1   single step using slow loaded instruction (specifically spm)
                      will generate break and 0x55 output when complete.
        ^ ^ ^
        | | '---- Single step - stop after 1 instruction
        | '------ Execute (at least initially) instruction loaded with D2.
        '-------- Use Flash (1) or virtual memory selected by C2 (0)


Resume execution:              60/61/79/7A 30
Resume from SW BP:             79 32
Step out:                      63 30
Execute instruction (via D2):  ?? 23
Read/write registers/SRAM:     66 20
Write single register:         66 21



Resuming execution

D0 00 00 xx -- set PC, xx = 40/60 - 41/61 - 59/79 - 5A/7A
D1 00 01 -- set breakpoint (single step in this case)
D0 00 00 30 -- set PC and GO







Writing a Flash Page

66
D0 00 1A D1 00 20 C2 05 20 03 01 05 40 00 00 -- Set X, Y, Z
D0 1F 00                                     -- Set PC to 0x1F00, inside the boot section to enable spm--

64
D2  01 CF  23        -- movw r24,r30
D2  BF A7  23        -- out SPMCSR,r26 = 03 = PGERS
D2  95 E8  33        -- spm

<00 55> 83 <55>

44 - before the first one
And then repeat the following until the page is full.

D0  1F 00            -- set PC to bootsection for spm to work
D2  B6 01  23 ll     -- in r0,DWDR (ll)
D2  B6 11  23 hh     -- in r1,DWDR (hh)
D2  BF B7  23        -- out SPMCSR,r27 = 01 = SPMEN
D2  95 E8  23        -- spm
D2  96 32  23        -- adiw Z,2


D0 1F 00
D2 01 FC 23 movw r30,r24
D2 BF C7 23 out SPMCSR,r28 = 05 = PGWRT
D2 95 E8 33 spm
<00 55>

D0 1F 00
D2 E1 C1 23 ldi r28,0x11
D2 BF C7 23 out SPMCSR,r28 = 11 = RWWSRE
D2 95 E8 33 spm
<00 55> 83 <55>
*/
