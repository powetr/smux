## 📑 1. Архитектура и Описание Комплекса
## smux мультиплексор консольных портов (до 8 мастеров с реальным оборудованием, до 4 реплик каждого, коллективное пользование)
* **Назначение:** Асинхронное разветвление физических консольных USB-портов (до 8 мастеров) на изолированные группы виртуальных терминалов (до 4 слейвов на мастер, например `/tmp/ttyCisco1`) для одновременной бесконфликтной работы студентов.
* **Отказоустойчивость:** Встроенная логика Hot-Plug. При отключении кабеля демон переходит в режим циклического ожидания (опрос каждые 5 секунд), не нагружая процессор. Сессии picocom у студентов при этом не падают.
* **Безопасность:** Встроенный контроль дубликатов путей в INI-конфигурации для защиты от пересечения потоков ввода-вывода.

## 📂 2. Файловая Структура в Системе
* `smux.c` — Исходный код демона на чистом асинхронном POSIX C.
* `/usr/local/bin/smux-daemon` — Скомпилированный рабочий бинарный файл службы.
* `/usr/local/bin/smux-status` — Инструмент оперативного мониторинга и вывода карты портов.
* `/etc/smux.conf` — Центральный конфигурационный файл оборудования.
* `/etc/systemd/system/smux.service` — Инициализационный юнит-файл Systemd.

⚙️ 3. Конфигурация Сервиса (/etc/systemd/system/smux.service)

[Unit]
Description=SMUX - Multi-Master Serial Port Multiplexer
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/smux-daemon /etc/smux.conf
Restart=always
RestartSec=5
User=root
PrivateTmp=false

[Install]
WantedBy=multi-user.target

🔧 4. Шаблон Конфигурации Оборудования (/etc/smux.conf)

[master1]
device = /dev/ttyUSB0
baudrate = 9600
slave1 = /tmp/ttyCisco1
slave2 = /tmp/ttyCisco2
slave3 = /tmp/ttyCisco3
slave4 = /tmp/ttyCisco4

[master2]
device = /dev/ttyUSB1
baudrate = 115200
slave1 = /tmp/ttyCisco5
slave2 = /tmp/ttyCisco6
slave3 = /tmp/ttyCisco7
slave4 = /tmp/ttyCisco8

🚀 5. Инструкция по Развертыванию и Управлению

mkdir -p /home/user/smux
cd /home/user/smux

Создайте файлы smux.c и smux-status, перенеся в них код выше.

sudo gcc -O2 -Wall -Wextra /home/mvv/smux/smux.c -o /usr/local/bin/smux-daemon

chmod +x /home/user/smux/smux-status
sudo cp /home/user/smux/smux-status /usr/local/bin/

sudo systemctl daemon-reload
sudo systemctl enable smux.service
sudo systemctl start smux.service

Просмотр интерактивного дерева портов:

root@stend02:/home/user/smux-status
=== SMUX SYSTEM STATUS ===
Daemon Status: RUNNING (PID: 21001)
--------------------------
[master1]
  Physical Port (/dev/ttyUSB0 @ 9600 baud): [ONLINE] (Active and Multiplexed)
  Available Student Links:
    ├── /tmp/ttyCisco1 -> /dev/pts/0
    ├── /tmp/ttyCisco2 -> /dev/pts/1
    ├── /tmp/ttyCisco3 -> /dev/pts/3
    ├── /tmp/ttyCisco4 -> /dev/pts/4

[master2]
  Physical Port (/dev/ttyUSB1 @ 9600 baud): [ONLINE] (Active and Multiplexed)
  Available Student Links:
    ├── /tmp/ttyCisco5 -> /dev/pts/7
    ├── /tmp/ttyCisco6 -> /dev/pts/8
    ├── /tmp/ttyCisco7 -> /dev/pts/9
    ├── /tmp/ttyCisco8 -> /dev/pts/10

Подключение
picocom -b 9600 /tmp/ttyCisco1
====пример=======
Idle Timer expired, Timing Out !!!
SW01-07 login: <129> 2000-01-02T01:17:13.910Z HWPKTRT-1-CPU RX rate limit (128) exceeded for queue 25 --
<129> 2000-01-02T01:23:14.040Z HWPKTRT-1-CPU RX rate limit (128) exceeded for queue 25 --

% Incorrect Login/Password
SW01-07 login:
====пример=======
Выход из терминала Ctrl-A + Ctrl-x

Другие пользователи которым нужен доступ к оборудованию в режиме мультиплексирования ( все видят вывод консоли мастера и могут управлять оборудованием) к другим слейвам мастера:
p
icocom -b 9600 /tmp/ttyCisco1 (1 терминал)
picocom -b 9600 /tmp/ttyCisco2 (2 терминал)
picocom -b 9600 /tmp/ttyCisco3 (3 терминал)
picocom -b 9600 /tmp/ttyCisco4 (4 терминал)









