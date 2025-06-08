f_ID = fopen("test.bin", 'rb', 'ieee-be');
fseek(f_ID, 2e6+1, "bof");

% Гайд по выбору le/be и fseek: если смотришь на бинарник и ноль самый
% первый - делай fseek нечетным. Иначе - четным. Про le/be пока не скажу
data = fread(f_ID, [2, 10e6], "uint16=>uint16");
% fseek(f_ID, 1, "bof");
lim = 1e6;
data = int16(data) - 2048;
figure(1)
tiledlayout(2,1)
nexttile
plot(data(1,1:lim))
title("Канал 1")
xlabel("Отсчеты")
ylabel("Код АПЦ (десятичный)")
grid on
nexttile
plot(data(2,1:lim))
title("Канал 2")
xlabel("Отсчеты")
ylabel("Код АПЦ (десятичный)")
grid on
fontname("Arial")
fclose(f_ID);

