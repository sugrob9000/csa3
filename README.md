# Лабораторная №3 на C++

Вариант
```lisp | risc | neum | hw | instr | binary | stream | mem | pstr | prob5 | pipeline```

С усложнением

## Язык программирования

Язык Lisp-подобный, с S-выражениями.
Типизации нет: все выражения целочисленные, 32-битные.

```bnf
program = paren | paren program

paren = "(" expr-list ")"

expr-list = expr | expr expr-list

expr = paren | number | identifier | string-literal
```
Названия правил `number`, `identifier`, `string-literal` говорят сами за себя.

Пользовательских функций нет, распознаются встроенные:

* `if` -- условное вычисление одного из двух выражений
* `while` -- цикл
* `set` -- установить значение переменной
* `alloc-static` -- выделить статическую память
* `print-string` -- напечатать P-строку
* `read-mem` -- прочитать память по адресу
* `write-mem` -- записать в память по адресу
* `progn` -- последовательное исполнение
* Арифметика: `+`, `-`, `*`, `/`, `%`. Последний -- взятие по модулю. `+` и `*` принимают произвольное число аргументов (хотя бы два)
* Сравнения `>`, `<`, `=`

## Компилятор

1. Преобразует текстовый поток в дерево ([1-parse.cpp](./compiler/1-parse.cpp))
2. Обходя дерево в аппликативном порядке, генерирует IR ([2-gen-ir.cpp](./compiler/2-gen-ir.cpp))
3. Раскрашивает значения в IR доступными регистрами ([3-codegen.cpp](./compiler/3-codegen.cpp))
4. Генерирует итоговый поток инструкций, преобразуя "высокоуровневые" IR-операции в инструкции ([3-codegen.cpp](./compiler/3-codegen.cpp))
5. Формирует финальный образ, готовый к загрузке в память процессора

IR тоже является потоком инструкций, но отличается от ISA процессора:
* IR оперирует над абстрактными переменными, которых может быть сколько угодно
* в IR есть `mov`, в ISA нет. Компилятор подбирает последовательность инструкций
в зависимости от того, отражены операнды в регистр, в память или в константу.
Например, `r0 <- r1` можно записать в ISA через `add`: `r0 <- r1 + 0`.
* и в IR, и в процессоре есть `store` и `load`, но в IR они генерируются только тогда,
когда исходный код явно обращается к памяти (`read-mem`, `write-mem`). На одну "явную"
операцию с памятью может прийтись до трёх в итоговом коде.

Стоит заметить, что, несмотря на неограниченное число вольно создаваемых переменных, IR
не является SSA-формой. Переменные можно переназначать после их создания. Также нет понятия
*basic block*; вся программа является одним блоков, внутри которого разрешены явные переходы.

## Процессор

Регистров 64 по 32 бита. Все регистры равноправны (хотя компилятор резервирует два под свои
нужды). Флагов нет; инструкция условного прыжка проверяет заданный (любой) регистр на
равенство нулю.

32 бита -- минимальная адресуемая единица (т.е. если считать байтом 8
бит, то 0x2 -- смещение на 8 байт, а адресуемое пространство -- 16 ГиБ).

Адресация существует только абсолютная.

Инструкции тоже по 32 бита. Существует 13 инструкций:

* `hlt` -- остановить выполнение
* бинарные операции арифметики и сравнения: `add`, `sub`, `mul`, `div`, `mod`, `equ`, `lt`, `gt`.
Принимают два входных значения и регистр-результат. Каждое входное значение может быть регистром
или константой (10 бит).
* `jmp`, безусловный переход. Адрес -- константа (28 бит)
* `jif`, условный переход. Адрес -- константа (22 бита), условие -- заданный регистр не 0

Схема бинарного представления инструкций (левее -- младшие биты):
```text
[4 opc]
= = = = - - - - - - - - - - - - - - - - - - - - - - - - - - - -
mem     [6 reg id ][1][21       absolute address              ]
LOAD, STORE         m (1 reg, 0 imm)
= = = = - - - - - - - - - - - - - - - - - - - - - - - - - - - -
binop   [6 dest id][1][10               ][1][10               ]
         dest id    m (1 reg, 0 imm)      m ( 1 reg, 0 imm)
ADD, SUB, MUL, DIV, MOD, EQU, LT, GT
= = = = - - - - - - - - - - - - - - - - - - - - - - - - - - - -
JMP     [28          imm absolute dest address                ]
JMP-IF  [6 cond id] [22     imm absolute dest address         ]
```

В процессоре реализован конвеер с 3 стадиями: fetch, decode, execute.
Соответственно, конвееризующие регистры:

* между fetch и decode (полученная инструкция)
* между decode и execute (регистр с управляющими сигналами).
* между execute и fetch (регистр с адресом следующего чтения инструкции)

Любой прыжок предполагает полную остановку конвеера, т.е. вставление двух execute-пузырей.

Поскольку память одноканальная, любая операция с памятью (`ld` или `st`) предполагает
приостановку fetch и, следовательно, выполняется два такта.

## Организация памяти

Архитектура фон Неймановская. Данные и код в одном адресном пространстве.

Было решено расположить данные в начале памяти, до кода (это упрощало компилятор).
Поскольку точка входа процессора -- адрес 0x0, то этот адрес резервируется под `jmp`
на основной код.

Адрес MMIO, привязанный к stdin/stdout эмулятора -- 0x3. Адреса 0x1 и 0x2 не используются
для MMIO, чтобы избежать конфликта с prefetch при пуске процессора.

```text
0x0    jmp 0x40 (адрес для примера, зависит от размера данных)
0x1    не используется
0x2    не используется
0x3    MMIO
--- начало данных ---
0x4    данные
...    ...
--- начало кода -----
0x40   код
...    ...
```

## Схемы процессора

### DataPath



### ControlUnit



## Тестирование

Производится с помощью Github actions: [ci.yml](./.github/workflows/ci.yml).

В Docker-контейнере с достаточно новой Ubuntu устанавливаются достаточно новые CMake, gcc,
clang-tidy, build-essential.

Проект конфигурируется в CI в релизе и с линтером clang-tidy. [Настройки
линтера](./clang-tidy-checks). Помимо прочего, проверяется "когнитивная сложность"
(метрика из clang-tidy, похожа на цикломатическую, но определяется по-другому).

Тесты прогоняются с помощью ctest.

## Подробный разбор программы

Рассмотрим [cat.lisp](./lisp/cat.lisp):

```
(while (set c (read-mem 3))
  (write-mem 3 c))
```

Компилятор оттранслирует её в следующий код (бинарник дизассемблирован утилитой [disasm](./disasm/main.cpp)):

```text
  0: 0x0000004b jmp 0x4
  1: [ unused ]
  2: [ unused ]
  3: [ MMIO ]
  4: 0x00000403 add r0, r0, 0x0
  5: 0x00000403 add r0, r0, 0x0
  6: 0x00001801 ld r0, mem[0x3]
  7: 0x00000413 add r1, r0, 0x0
  8: 0x00000c08 equ r0, r1, 0x0
  9: 0x0000400c jif r0, 0x10
  a: 0x00000fe3 add r62, r1, 0x0
  b: 0x00001bf3 add r63, 0x3, 0x0
  c: 0x00000403 add r0, r0, 0x0
  d: 0x00000403 add r0, r0, 0x0
  e: 0x0001ffe2 st r62, mem[r63]
  f: 0x0000004b jmp 0x4
 10: 0x00000000 halt 0x0
```
