#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <dos.h>
#include <windows.h> 
#include <ftd2xx.h>
#include <time.h>
// #include <minwindef.h>


#define PacketSize 512
#define NumSamples 1000000000
#define PacketCoef 1 // (пока 1, но должно быть 2) потому что один отсчет будет преобразован в два 8-битных отсчета
#define rxTotal NumSamples*PacketCoef

FT_HANDLE Handle1;
FT_STATUS myftStatus;
UCHAR MASK = 0xFF; // Я В ДУШЕ НЕ ЕБУ, ДОЛЖНЫ БЫТЬ ЗДЕСЬ ВСЕ НУЛИ ИЛИ ВСЕ ЕДИНИЦЫ (0xFF)
UCHAR Mode;
UCHAR LatTimer = 64;
    DWORD sig1 = 0x00000000;
    DWORD sig2 = 0xFFFFFFFF;

// char *rxBuffer;

DWORD EventWord;
DWORD rxBytes;
DWORD txBytes;
LPDWORD BytesReceived;
static FT_PROGRAM_DATA datastruct;
PUCHAR gotBitMode;

FILE *fp;
void printEEPdata(FT_HANDLE Handle);
void delay(int milliseconds);
volatile sig_atomic_t stop = 0;
size_t total_size = (size_t)1024 * (size_t)1024 * (size_t)1024 * 2;

// Обработчик сигнала SIGINT
void handle_sigint(int sig) {
    stop = 1; // Устанавливаем флаг для выхода из цикла
}

int main(){
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);

    BytesReceived = (LPDWORD)malloc(sizeof(LPDWORD));
    gotBitMode = (PUCHAR)malloc(sizeof(PUCHAR));
    size_t NumWrite = 0;
    unsigned long int numBytes = 0;
    
    // Установка обработчика сигнала SIGINT (чтобы по Ctrl+C выходить из приёмного цикла. Придумано нейронкой)
    signal(SIGINT, handle_sigint);
    LPVOID rxBuffer   = (LPVOID) _aligned_malloc(65536 * sizeof(unsigned char)*PacketCoef + 1024, 64);
    LPVOID dataBuffer = (LPVOID) _aligned_malloc(total_size * sizeof(unsigned char)*PacketCoef + 65536, 64); // буфер

    memset(rxBuffer, (unsigned char)0, 65536 * sizeof(unsigned char)*PacketCoef + 1024);
    memset(dataBuffer, (unsigned char)0, total_size * sizeof(unsigned char)*PacketCoef + 65536);
    
    if (rxBuffer == NULL || dataBuffer == NULL) exit(1);
    
    fp = fopen("test.bin", "wb");
    if (fp == NULL) {
        printf("Cannot open file.\n");
        goto endProg;
    }
    
    // "FT9MR6CDA" - плата. "219DJ74T" - Аlinx
    myftStatus = FT_OpenEx("219DJ74T", FT_OPEN_BY_SERIAL_NUMBER, &Handle1); // Открытие по серийнику. Иначе конфликты с программатором ПЛИС (он тоже на FTDI сделан)
    if (!FT_SUCCESS(myftStatus)){
        printf("err no %ld while opening device\n", myftStatus);
        goto endProg;
    }
    // myftStatus = FT_ResetDevice(Handle1);
    // if (myftStatus != FT_OK) {
    //     // Обработка ошибки сброса
    //     printf("Error resetting\n");
    //     FT_Close(Handle1);
    //     return 1;
    // }
    
    myftStatus = FT_OpenEx("219DJ74T", FT_OPEN_BY_SERIAL_NUMBER, &Handle1); // Открытие по серийнику. Иначе конфликты с программатором ПЛИС (он тоже на FTDI сделан)
    // myftStatus = FT_Open(0, &Handle1);
    if (!FT_SUCCESS(myftStatus)){
        printf("err no %ld while opening device\n", myftStatus);
        goto endProg;
    }


    // // // // // // // //
    // CODE STARTS HERE  // 
    // // // // // // // //

    printEEPdata(Handle1);
    
    if (!FT_SUCCESS(FT_SetBitMode(Handle1, MASK, FT_BITMODE_RESET))){
        printf("error #%i while resetting\n", myftStatus);
        goto endProg;
    }

    delay(250);

    if (!FT_SUCCESS(FT_SetBitMode(Handle1, MASK, FT_BITMODE_SYNC_FIFO))){
        printf("error #%i while setting sync FIFO mode\n", myftStatus);
        goto endProg;
    }
    else{
        myftStatus = FT_GetBitMode(Handle1, gotBitMode);
            if (!FT_SUCCESS(myftStatus)){
                printf("error #%i while trying to get bitmode\n", myftStatus);
            goto endProg;
            }
        
        printf("received bitmode: %p\n", gotBitMode);
        
        printf("setting up some options after status %i\n", myftStatus);
        
        if (!FT_SUCCESS(FT_SetLatencyTimer(Handle1, LatTimer))){
            printf("error #%i while setting latency timer\n", myftStatus);
            goto endProg;
        }

        if (!FT_SUCCESS(FT_SetTimeouts(Handle1, 1000, 1000))){
        printf("error #%i while setting timeouts\n", myftStatus);
        goto endProg;
        }

        if (!FT_SUCCESS(FT_SetUSBParameters(Handle1, 0x4000, 0x4000))){
            printf("error #%i while setting USB parameters\n", myftStatus);
            goto endProg;
        }
        
        // Настройка была отключена, ибо она только для UART как я понял
        // myftStatus = FT_SetFlowControl(Handle1, FT_FLOW_RTS_CTS, 0, 0);
        // if (!FT_SUCCESS(myftStatus)){
        // printf("error #%i while setting flow control\n", myftStatus);
        // goto endProg;
        // }
    }
    // FT_SetBaudRate(Handle1, 9600); // не влияет в FIFO режиме, но требуется
    // FT_Purge(Handle1, FT_PURGE_TX | FT_PURGE_RX);

    printf("Trying to receive bytes\n");
    size_t requested = (total_size < NumSamples) ? total_size : NumSamples;
    while(numBytes < requested){
            if (stop) {
                printf("Reception interrupted by user.\n");
                break; // Выход из цикла по Ctrl+C
            }
            // FT_Read(Handle1, dataBuffer + numBytes, 0x4000, BytesReceived);
            // FT_GetStatus(Handle1, &rxBytes, &txBytes, EventWord);
            // FT_GetQueueStatus(Handle1, &rxBytes);
            if ( (FT_SUCCESS(FT_GetStatus(Handle1, &rxBytes, &txBytes, &EventWord))) && (rxBytes >= PacketSize) ){
                // myftStatus = // Раскоммент, если оч хочется проверить статус
                FT_Read(Handle1, dataBuffer + numBytes, rxBytes, BytesReceived);
                // FT_Write(Handle1, dataBuffer, 0x4000, BytesReceived);
                numBytes += *BytesReceived;
            }
    }

    printf("left receiving loop. Saving data...\n");
    NumWrite = fwrite(dataBuffer, sizeof(char), numBytes, fp);
    printf("Successfully written %lu bytes\n", NumWrite);
    
    
    endProg:
    // CLOSING DEVICE
    if (!FT_SUCCESS(FT_Close(Handle1)))
        printf("err no %ld while closing device\n", myftStatus);
    else printf("Device closed successful, code %ld\n", myftStatus);

    fclose(fp);
    _aligned_free(rxBuffer);
    _aligned_free(dataBuffer);
    
    return 0;
}

void delay(int milliseconds)
{
    long pause;
    clock_t now,then;

    pause = milliseconds*(CLOCKS_PER_SEC/1000);
    now = then = clock();
    while( (now-then) < pause )
        now = clock();
}


void printEEPdata(FT_HANDLE Handle)
{
    datastruct.Signature1 = sig1;
    datastruct.Signature2 = sig2;
    datastruct.Manufacturer = (char *)malloc(256); /* E.g "deponce" */
    datastruct.ManufacturerId = (char *)malloc(256); /* E.g. "FT" */
    datastruct.Description = (char *)malloc(256); /* E.g. "USB HS Serial Converter" */
    datastruct.SerialNumber = (char *)malloc(256); /* E.g. "FT000001" if fixed, or NULL */
    if (datastruct.Manufacturer == NULL ||
        datastruct.ManufacturerId == NULL ||
        datastruct.Description == NULL ||
        datastruct.SerialNumber == NULL)
    {
        printf("Failed to allocate memory.\n");
        exit(1);
    }
    
    if (!FT_SUCCESS(FT_EE_Read(Handle, &datastruct))){
        printf("error #%i while reading EEPROM\n", myftStatus);
        exit(1);
    }

    printf("Program Data Version: %x \n", datastruct.Version);
    printf("FT_EE_Read succeeded.\n\n");
    printf("Signature1(must be 00000000) = %d\n", (int)datastruct.Signature1);          
    printf("Signature2(must be ffffffff)= %d\n", (int)datastruct.Signature2);
    printf("Version = %d\n", (int)datastruct.Version);
    printf("VendorId = 0x%04X\n", datastruct.VendorId);               
    printf("ProductId = 0x%04X\n", datastruct.ProductId);
    printf("Manufacturer = %s\n", datastruct.Manufacturer);           
    printf("ManufacturerId = %s\n\n", datastruct.ManufacturerId);
    printf("this 2 values is nonzero if mode is 245 FIFO and 245 FIFO CPU target respectievly: %x  , %x \n", (int)datastruct.IsFifoH, (int)datastruct.IFBIsFifoTar7);
    printf("this 2 values is nonzero if A and B ports is to use VCP drivers: %x  , %x \n", datastruct.AIsVCP7, datastruct.BIsVCP7);

    free(datastruct.Manufacturer);
    free(datastruct.ManufacturerId);
    free(datastruct.Description);
    free(datastruct.SerialNumber);
}