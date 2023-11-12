# Лабораторная №3 на C++

## Зависимости

- [fmt](https://fmt.dev/latest/index.html)

Зависимости можно установить системным пакетным менеджером либо C++-специфичным (vcpkg,
conan). Подойдёт любой способ, делающий пакеты доступными для `find_package`.

## Сборка

Обычная конфигурация:
```sh
cmake -B build
```

Сконфигурировать в релизе:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

Сконфигурировать с линтером clang-tidy (сборка намного медленнее):
```sh
cmake -B build-tidy -DENABLE_CLANG_TIDY=ON
```

После конфигурации собрать
```sh
cmake --build <build-dir>
```

Прогнать тесты:
```sh
ctest --test-dir <build-dir>
```
