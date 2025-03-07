#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <dos.h>
#include <windows.h> 
#include <ftd2xx.h>
#include <time.h>
// #include <minwindef.h>


#define PacketSize 512 // Минимальный объём данных, подлежащий передаче
#define NumSamples 1000000000
#define PacketCoef 1 // TODO: (пока 1, но должно быть 2) потому что один отсчет будет преобразован в два 8-битных отсчета
#define rxTotal NumSamples*PacketCoef // общее количество семплов. Следует использовать именно его


// наибольший объём буфера в ОЗУ. Используется для буферизации всей выборки в ОЗУ в процессе приёма
// данные "упаковываются" в бинарник только после завершения приёма(легаси со времён поиска причин 
// потерь данных). 
// TODO: писать сразу в бинарник
size_t total_size = (size_t)1024 * (size_t)1024 * (size_t)1024 * 2; 


// TODO: сделать переменные локальными
FT_HANDLE Handle1; // хэндл, по которому осуществляется общение с FTDI
FT_STATUS myftStatus; // в него пишутся return-коды


// Прототипы пользовательских функций
// TODO: перенести прототипы в пользовательский header-файл
void printEEPdata(FT_HANDLE);
void printDevices();
void delay(int);
void setupHandledDevice(FT_HANDLE);

// Переменная, являющаяся частью имплементации способа выхода из цикла передачи данных, придуманного нейронкой
volatile sig_atomic_t stop = 0;


// Обработчик сигнала SIGINT. Дрянь, придуманная нейронкой для выхода из цикла по нажатию Ctrl + C
void handle_sigint(int sig) {
    stop = 1; // Устанавливаем флаг для выхода из цикла
    printf("Reception interrupted by user.\n");
}

int main(){
    // Запрашиваем для процесса приоритет реального времени. Без прав администратора 
    // программа запустится с высоким приориетом.
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);

    FILE *fp; // Указатель на бинарный файл, куда будут сохраняться данные

    DWORD rxBytes = 0; // переменная, содержащая в себе число данных, готовых к отправке В  ПК
    DWORD txBytes = 0; // переменная, содержащая в себе число данных, готовых к отправке ИЗ ПК. Что-то типа очереди передачи
    LPDWORD BytesReceived = 0; // переменная, содержащая в себе число байт, которые были считаны за вызов FT_Read
    
    
    size_t numBytes = 0; // переменная, содержащая в себе число байт, принятых за цикл передачи данных 
    
    // Установка обработчика сигнала SIGINT (чтобы по Ctrl+C выходить из приёмного цикла. Придумано нейронкой)
    signal(SIGINT, handle_sigint);

    // выровненный буфер. Нужен для использования SIMD. 
    // TODO: уменьшить буфер до размера одной пачки передаваемых данных (задаётся в FT_SetUSBParameters())
    LPVOID dataBuffer = (LPVOID) _aligned_malloc(total_size * sizeof(unsigned char)*PacketCoef + 65536, 64); 
    // Проверка успешности выделения памяти. При неуспешном выделении случается выход
    if (dataBuffer == NULL) {
        printf("Cannot allocata dataBuffer. Exiting\n");
        exit(1);
    }
    // Заполняем буфер нулями, чтобы память была выделена сразу, а не в процессе обращения к элементам буфера
    memset(dataBuffer, (unsigned char)0, total_size * sizeof(unsigned char)*PacketCoef + 65536);
    
    // Открываем файл для записи в бинарном режиме 
    fp = fopen("test.bin", "wb");
    if (fp == NULL) {
        printf("Cannot open file to write data.\n");
        goto endProg;
    }
    
    // Выводим информацию обо всех подключенных устройствах FTDI. Особенно полезно, если нужно узнать серийник 
    // устройства, которое следует открыть (полезно, когда к ПК подключено сразу несколько устройств FTDI)
    printDevices();

    // "FT9MR6CDA" - QMTech (именно A, поскольку устройство воспринимается как два с литерами A и B). 
    // "219DJ74T"  - Аlinx
    // Открытие по серийнику. При использовании FT_Open() у меня возникали конфликты с программатором
    // ПЛИС
    myftStatus = FT_OpenEx("219DJ74T", FT_OPEN_BY_SERIAL_NUMBER, &Handle1); 
    if (!FT_SUCCESS(myftStatus)){
        printf("err no %ld while opening device\n", myftStatus);
        goto endProg;
    }

    // // // // // // // //
    // CODE STARTS HERE  // 
    // // // // // // // //
    
    // Вывод данных, сохранённых в EEPROM. Полезно для выяснения, есть ли вообще контакт с микросхемой
    printEEPdata(Handle1);
    
    // Настройка микросхемы (режим, таймауты, размеры буферов и т.д.)
    setupHandledDevice(Handle1);

    
    printf("Trying to receive bytes\n");
    
    // Проверяем, что больше - total_size или rx_total. Нужно поскольку 
    // total_size жёстко ограничивает объём выделяемой ОЗУ, тогда как 
    // rxTotal подразумевает непосредственное регулярное изменение пользователем
    // Т.е. можно запросить объём данных, который не поместится в ОЗУ, а за счёт
    // этой строчки будет принят вмещающийся объём данных.
    size_t requested = (total_size < rxTotal) ? total_size : rxTotal;
    
    // Начало цикла передачи данных
    // В это время программа ничего не выводит,
    // TODO: добавить вывод информации о процессе передачи данных
    while(numBytes < requested){
            if (stop)                
                break; // Выход из цикла по Ctrl+C
            // Запрашиваем у устройства информацию об объёме данных, который готов
            // к передаче, и, если он достаточен, читаем этот объём
            if (FT_SUCCESS(FT_GetQueueStatus(Handle1, &rxBytes)) && rxBytes >= PacketSize)
                FT_Read(Handle1, dataBuffer + numBytes, rxBytes, BytesReceived);
            numBytes += *BytesReceived; // добавляем число принятых за вызов FT_Read() байт в общее число принятых.
    }

    printf("left receiving loop. Saving data...\n");
    // Сохраняем данные в файл.
    size_t NumWrite = fwrite(dataBuffer, sizeof(char), numBytes, fp);
    printf("Successfully written %lu bytes\n", NumWrite);
    
    
    endProg:
    // CLOSING DEVICE
    if (!FT_SUCCESS(FT_Close(Handle1)))
        printf("err no %ld while closing device\n", myftStatus);
    else printf("Device closed successful, code %ld\n", myftStatus);

    // Освобождаем ресурсы, которые необходимо освободить.
    fclose(fp);
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

void setupHandledDevice(FT_HANDLE passedHandle){
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