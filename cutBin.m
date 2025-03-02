inputFile = 'test.bin';   % Исходный файл
outputFile = 'testCut.bin'; % Целевой файл
numSamples = 1e6;          % Количество отсчетов (1 млн)
offset = 3e6;
dataType = 'uint16=>uint16';        % Тип данных (замените на ваш)


fileID = fopen(inputFile, 'rb');
fseek(fileID, offset, 'bof');
data = fread(fileID, numSamples, dataType);
fclose(fileID); % Закройте файл

if length(data) < numSamples
    error('Недостаточно данных в исходном файле!');
end

fileID = fopen(outputFile, 'wb');
fwrite(fileID, data, 'uint16');
fclose(fileID); % Закройте файл

disp('Копирование завершено!');