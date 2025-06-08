f_ID = fopen("test3G(22_40).bin", 'rb', 'ieee-be');
% fseek(f_ID, 1, "bof");
data = fread(f_ID, [2, 1e6], "uint16=>uint16");
data = int16(data) - 2048;
data = double(data);
plot(data(2,:))

fclose(f_ID);