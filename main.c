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
#define PacketCoef 1 // TODO: (пока 1, но должно быть 2) потому что один отсчет будет преобразован в два 8-битных отсчета
#define rxTotal NumSamples*PacketCoef
size_t total_size = (size_t)1024 * (size_t)1024 * (size_t)1024 * 2;


FT_HANDLE Handle1;
FT_STATUS myftStatus;


void printEEPdata(FT_HANDLE Handle);
void printDevices();
void delay(int milliseconds);
volatile sig_atomic_t stop = 0;


// Обработчик сигнала SIGINT. Дрянь, придуманная нейронкой для выхода из цикла по нажатию Ctrl + C
void handle_sigint(int sig) {
    stop = 1; // Устанавливаем флаг для выхода из цикла
}

int main(){
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    FILE *fp;

    DWORD rxBytes = 0;
    DWORD txBytes = 0;
    LPDWORD BytesReceived = 0;
    
    size_t NumWrite = 0;
    unsigned long int numBytes = 0;
    
    // Установка обработчика сигнала SIGINT (чтобы по Ctrl+C выходить из приёмного цикла. Придумано нейронкой)
    signal(SIGINT, handle_sigint);
    LPVOID rxBuffer   = (LPVOID) _aligned_malloc(65536 * sizeof(unsigned char)*PacketCoef + 1024, 64);
    LPVOID dataBuffer = (LPVOID) _aligned_malloc(total_size * sizeof(unsigned char)*PacketCoef + 65536, 64); // буфер

    if (rxBuffer == NULL || dataBuffer == NULL) exit(1);
    memset(rxBuffer, (unsigned char)0, 65536 * sizeof(unsigned char)*PacketCoef + 1024);
    memset(dataBuffer, (unsigned char)0, total_size * sizeof(unsigned char)*PacketCoef + 65536);
    
    
    fp = fopen("test.bin", "wb");
    if (fp == NULL) {
        printf("Cannot open file to write data.\n");
        goto endProg;
    }
    
    printDevices();

    // "FT9MR6CDA" - QMTech (именно A, поскольку устройство воспринимается как два с литерами A и B). 
    // "219DJ74T"  - Аlinx
    myftStatus = FT_OpenEx("219DJ74T", FT_OPEN_BY_SERIAL_NUMBER, &Handle1); // Открытие по серийнику. Иначе конфликты с программатором ПЛИС (он тоже на FTDI сделан)
    if (!FT_SUCCESS(myftStatus)){
        printf("err no %ld while opening device\n", myftStatus);
        goto endProg;
    }

    // // // // // // // //
    // CODE STARTS HERE  // 
    // // // // // // // //

    printEEPdata(Handle1);
    
    setupHandledDEvice(Handle1);


    printf("Trying to receive bytes\n");
    size_t requested = (total_size < NumSamples) ? total_size : NumSamples;
    while(numBytes < requested){
            if (stop) {
                printf("Reception interrupted by user.\n");
                break; // Выход из цикла по Ctrl+C
            }
            FT_GetQueueStatus(Handle1, &rxBytes);
            // if (rxBytes != 0 || txBytes != 0)
            //     printf("RX: %lu bytes, TX: %lu bytes\n");
            FT_Read(Handle1, dataBuffer + numBytes, rxBytes, BytesReceived);
            
            numBytes += *BytesReceived;
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
    FT_PROGRAM_DATA datastruct;
    DWORD sig1 = 0x00000000;
    DWORD sig2 = 0xFFFFFFFF;

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

void printDevices(){
    FT_STATUS ftStatus; 
    FT_DEVICE_LIST_INFO_NODE *devInfo; 
    DWORD numDevs;  
    
    // create the device information list 
    ftStatus = FT_CreateDeviceInfoList(&numDevs);  
    if (ftStatus == FT_OK) 
        printf("Number of devices is %d\n",numDevs);
     
    if (numDevs > 0) {  // allocate storage for list based on numDevs  
        devInfo = (FT_DEVICE_LIST_INFO_NODE*)malloc(sizeof(FT_DEVICE_LIST_INFO_NODE)*numDevs);   
        // get the device information list  
        ftStatus = FT_GetDeviceInfoList(devInfo,&numDevs);   
        if (ftStatus == FT_OK) {  
             for (int i = 0; i < numDevs; i++) {    
                printf("Dev %d:\n",i);     
                printf("  Flags=0x%x\n",devInfo[i].Flags);     
                printf("  Type=0x%x\n",devInfo[i].Type);     
                printf("  ID=0x%x\n",devInfo[i].ID);     
                printf("  LocId=0x%x\n",devInfo[i].LocId);     
                printf("  SerialNumber=%s\n",devInfo[i].SerialNumber);    
                printf("  Description=%s\n",devInfo[i].Description);     
                printf("  ftHandle=0x%x\n",devInfo[i].ftHandle);    
            } 
        } 
    }
    else printf("No devices detected!\n");
    free(devInfo);
}

void setupHandledDEvice(FT_HANDLE passedHandle){
    UCHAR MASK = 0xFF;
    PUCHAR gotBitMode = (PUCHAR)malloc(sizeof(PUCHAR)); // Legacy со времён, когда я параноил по поводу всего, потому что не ничего работало
    UCHAR LatTimer = 64;

    if (!FT_SUCCESS(FT_SetBitMode(passedHandle, MASK, FT_BITMODE_RESET))){
        printf("error #%i while resetting. Exiting\n", myftStatus);
        exit(1);
    }

    delay(250);

    if (!FT_SUCCESS(FT_SetBitMode(passedHandle, MASK, FT_BITMODE_SYNC_FIFO))){
        printf("error #%i while setting sync FIFO mode. Exiting\n", myftStatus);
        exit(1);
    }
    else {
        myftStatus = FT_GetBitMode(passedHandle, gotBitMode);
            if (!FT_SUCCESS(myftStatus)){
                printf("error #%i while trying to get bitmode. Exiting\n", myftStatus);
                exit(1);
            }
        
        printf("received bitmode: %p\n", gotBitMode);
        
        printf("setting up some options after status %i\n", myftStatus);
        
        if (!FT_SUCCESS(FT_SetLatencyTimer(passedHandle, LatTimer))){
            printf("error #%i while setting latency timer. Exiting\n", myftStatus);
            exit(1);
        }

        if (!FT_SUCCESS(FT_SetTimeouts(passedHandle, 1000, 1000))){
            printf("error #%i while setting timeouts. Exiting\n", myftStatus);
            exit(1);
        }

        if (!FT_SUCCESS(FT_SetUSBParameters(passedHandle, 0x4000, 0x4000))){
            printf("error #%i while setting USB parameters. Exiting\n", myftStatus);
            exit(1);
        }
    }
}