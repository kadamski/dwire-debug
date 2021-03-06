// SerialPort.c






//// Serial port access.


#if windows
  char portfilename[] = "//./COMnnn";

  void MakeSerialPort(char *portname, int baudrate) {
    strncpy(portfilename+4, portname, sizeof(portfilename)-5);
    portfilename[sizeof(portfilename)-1] = 0;
    SerialPort = CreateFile(portfilename, GENERIC_WRITE | GENERIC_READ, 0,0, OPEN_EXISTING, 0,0);
    if (SerialPort==INVALID_HANDLE_VALUE) {
      DWORD winError = GetLastError();
      Ws("Couldn't open ");
      Ws(portname);
      Ws(": ");
      WWinError(winError);
      Fail("");
    }

    DCB dcb = {sizeof dcb, 0};
    dcb.fBinary  = TRUE;
    dcb.BaudRate = baudrate;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    if (!SetCommState(SerialPort, &dcb)) {
      // This is probably not an FT232
      CloseHandle(SerialPort); SerialPort = 0; return;
    }

    WinOK(SetCommTimeouts(SerialPort, &(COMMTIMEOUTS){300,300,1,300,1}));
  }

  void CloseSerialPort() {if (SerialPort) {CloseHandle(SerialPort); SerialPort = 0;}}
#else
  #include <stropts.h>
  #include <asm/termios.h>
  void MakeSerialPort(char *portname, int baudrate) {
    char fullname[256] = "/dev/";
    strncat(fullname, portname, 250); fullname[255] = 0;
    if ((SerialPort = open(fullname, O_RDWR/*|O_NONBLOCK|O_NDELAY*/)) < 0) {Fail("Couldn't open serial port.");}
    struct termios2 config = {0};
    if (ioctl(SerialPort, TCGETS2, &config)) {Close(SerialPort); SerialPort = 0; return;}
    config.c_cflag     = CS8 | BOTHER | CLOCAL;
    config.c_iflag     = IGNPAR | IGNBRK;
    config.c_oflag     = 0;
    config.c_lflag     = 0;
    config.c_ispeed    = baudrate;
    config.c_ospeed    = baudrate;
    config.c_cc[VMIN]  = 200;         // Nonblocking read of up to 255 bytes
    config.c_cc[VTIME] = 5;           // 0.5 seconds timeout
    if (ioctl(SerialPort, TCSETS2, &config)) {Close(SerialPort); SerialPort = 0; return;}
    usleep(10000); // Allow 10ms for USB to settle.
    ioctl(SerialPort, TCFLSH, TCIOFLUSH);
  }

  void CloseSerialPort() {if (SerialPort) {Close(SerialPort); SerialPort = 0;}}
#endif


void SerialWrite(const u8 *bytes, int length) {
  Write(SerialPort, bytes, length);
}

void SerialRead(u8 *buf, int len) {
  int totalRead = 0;
  do {
    int lengthRead = Read(SerialPort, buf+totalRead, len-totalRead);
    if (lengthRead == 0) {
      Ws("SerialRead expected ");
      Wd(len,1); Ws(" bytes, received ");
      Wd(totalRead,1); Ws(" bytes from "); Ws(UsbSerialPortName);
      if (totalRead) {Wl(); for (int i=0; i<totalRead; i++) {Wx(buf[i],2); Ws("  ");}}
      Fail("");
    }
    totalRead += lengthRead;
  } while(totalRead < len);
}

void SerialBreak(int period) {
#ifdef windows
  WinOK(SetCommBreak(SerialPort));
  Sleep(period);
  WinOK(ClearCommBreak(SerialPort));
#else
  ioctl(SerialPort, TCFLSH, TCIOFLUSH);
  ioctl(SerialPort, TIOCSBRK);
  usleep(period*1000);
  ioctl(SerialPort, TIOCCBRK);
#endif
}

void SerialDump() {
  u8 byte;
  while (1) {
    if (Read(SerialPort, &byte, 1)) {Wx(byte,2); Wc(' ');}
    else                            {Wsl("."); return;}
  }
}
