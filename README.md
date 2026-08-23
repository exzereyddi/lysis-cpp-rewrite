# 🔧 **fck** — AMX Mod X / SourcePawn Decompiler (C++ Port)

![Status](https://img.shields.io/badge/Status-Stable-brightgreen?style=flat-square)
![Base](https://img.shields.io/badge/Base-Lysis%20(Java)-blueviolet?style=flat-square)
[![Made with C++](https://img.shields.io/badge/Made%20with-C%2B%2B17-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)]()
[![Platform Windows](https://img.shields.io/badge/Platform-Windows-0078D6?style=flat-square&logo=windows&logoColor=white)]()
[![MSVC](https://img.shields.io/badge/Compiler-MSVC%202022-5C2D91?style=flat-square)]()

<p align="center">
  <img src="https://readme-typing-svg.demolab.com?font=Fira+Code&pause=1000&color=00FF88&center=true&vCenter=true&width=500&lines=fck+%E2%80%94+lysis-cpp+port;.amxx+%2F+.smx+decompiler;Fast%2C+standalone%2C+no+Java+required" alt="Typing SVG" />
</p>

> [!IMPORTANT]
> Это **порт декомпилятора [Lysis](https://github.com/alliedmodders/lysis)** (оригинал на Java) на **чистый C++17**.
>
> **Назначение:** декомпиляция плагинов AMX Mod X (`.amxx`) и SourcePawn (`.smx`) без JVM. Быстро, автономно, drag&drop.

## 📑 Содержание
- [Зачем этот форк](#зачем-этот-форк)
- [Что умеет fck](#что-умеет-fck)
- [Как использовать](#как-использовать)
- [Как собрать из исходников](#как-собрать-из-исходников)
- [Как работает](#как-работает)
- [Ограничения](#ограничения)
- [Дисклеймер](#дисклеймер)

## Зачем этот форк
Оригинальный [Lysis](https://github.com/alliedmodders/lysis) написан на **Java** — нужен JRE, запускается через `java -jar`, работает медленно на больших плагинах. Хочется просто **`.exe`**, с которым дважды кликнуть, перетащить файл и получить исходник.

### Основные цели порта
1. **Автономный `.exe`** — без Java, без зависимостей, ~350 KB
2. **Drag & drop интерфейс** — консолька остаётся открытой, кидай файлы один за другим
3. **Стабильность** — SEH translator ловит любые падения на битых/обфусцированных байтах, программа не крашится
4. **Читаемость вывода** — распознаёт строковые массивы AMXX (indirection), автор/версия плагина, метадата

### Как fck помогает
| Задача | Решение |
|---|---|
| 🔍 **Изучить чужой плагин** | Один клик → готовый `.sma` рядом с exe |
| 🐛 **Найти баг в скомпиленном плагине** | Смотришь декомпилированный код, ищешь логику |
| 📚 **Восстановить утерянные исходники** | 100% функций декомпилируются в реальных плагинах с debug-info |
| 🎓 **Разобраться как работает Pawn VM** | Читаешь исходники fck — весь пайплайн виден |
| ⚡ **Автоматизация** | Батч-режим через аргументы: `fck.exe *.amxx` |

---

## Что умеет fck

### Форматы
| Формат | Поддержка | Примечания |
|---|---|---|
| ✅ **AMXX** (`.amxx`) | Полная | AMX Mod X плагины, распаковка gzip, debug info |
| ✅ **AMX** (`.amx`) | Полная | Голый AMX формат |
| ⚠️ **SourcePawn** (`.smx`) | Частичная | Парсинг заголовка, декомпиляция — TODO |

### Возможности
- 🔄 **Распаковка gzip** секций через встроенный miniz (без внешних зависимостей)
- 🧩 **Декомпиляция байткода** — восстановление `if/else`, `while`, `do-while`, `switch`, `goto`
- 🎯 **Type propagation** — Forward + Backward, восстановление `Float:`, `bool:`, `String:` тегов
- 📝 **Умное распознавание строк** — обычные строки, char-массивы, AMXX indirection vectors
- 🎨 **Печать выражений** — операторы, тернарники, логические цепочки `&&`/`||`
- 🧠 **Восстановление имён** — из debug-info (variables, globals, functions, natives)
- 💾 **Автосохранение** — `.sma` рядом с `.exe`, имя `<plugin>_decompile.sma`
- ⚡ **Быстро** — типичный плагин (156 функций) декомпилируется за ~1.5 секунды
- 🛡 **Отказоустойчиво** — SEH translator, per-function try/catch — программа не падает никогда

---

## Как использовать

### Способ 1: Drag & Drop
1. Скачай `fck.exe`
2. Дважды кликни — откроется консоль
3. Перетащи `.amxx` файл в окно, нажми Enter
4. Рядом с `fck.exe` появится `<name>_decompile.sma`
5. Перетаскивай следующий файл или нажми Enter на пустой строке чтобы выйти

### Способ 2: Батч из командной строки
```bash
fck.exe plugin.amxx
fck.exe plugin1.amxx plugin2.amxx plugin3.amxx
fck.exe "C:\path with spaces\my_plugin.amxx"
```

### Пример вывода
**Консоль:**
```
> D:\plugins\client_analyzer.amxx

=== D:\plugins\client_analyzer.amxx ===
Plugin: Client Analyzer v2.8.1 by FAME
// [1/156] abs
// [21/156] LogError
// [41/156] PrintToAdmins
...
// [156/156] plugin_end

Decompiled: 156 ok, 0 errors
Written: client_analyzer_decompile.sma (287 KB) in 1.42s
>
```

**Header в `.sma`:**
```pawn
// Decompile by fck
// Decompiled: 2026-07-08 15:24:33 (took 1.42s)
// Plugin: Client Analyzer v2.8.1 by FAME
```

---

## Как собрать из исходников

### Требования
- **Visual Studio 2022** (Community / Professional / Enterprise)
- **Windows 10/11 SDK**
- **C++17** (уже настроено в `.vcxproj`)

### Шаги
1. Клонируй репозиторий (или скачай ZIP)
2. Открой `fck.sln` в Visual Studio
3. Выбери конфигурацию: **Release / x64**
4. Build → Rebuild Solution (Ctrl+Alt+F7)
5. Готовый `fck.exe` появится в `Release/`

### Особенности сборки
- **Async exception handling** включён (нужен для SEH translator)
- **Language standard:** ISO C++17
- **miniz** встроен как исходник в `third_party/`, никаких DLL

---

## Как работает

Полный пайплайн декомпиляции одной функции:

1. **AMXModXFile** — распаковка gzip, парсинг заголовков, debug info
2. **MethodParser** — байткод (Pawn opcodes) → LIR (LInstruction + LBlock)
3. **BlockAnalysis** — Order, Reducibility, Dominators, Loops, StackBalance
4. **NodeBuilder** — LIR → NodeGraph (AST-подобная структура из D-узлов)
5. **NodeAnalysis + NodeRewriter** — dead code, collapse arrays, float natives → binary
6. **ForwardTypePropagation + BackwardTypePropagation** — восстановление типов (`Float:`, `bool:`, `String:`)
7. **NodeRenamer** — именование временных переменных
8. **SourceStructureBuilder** — NodeGraph → ControlBlock (if/while/switch/return)
9. **SourceBuilder** — ControlBlock → текст на SourcePawn
10. Результат → `plugin_decompile.sma`

### Ключевые фичи ниже уровня API
| Фича | Где реализовано |
|---|---|
| **AMXX indirection vectors** для строковых массивов `[N][]` | `source_builder.cpp` → `writeGlobal` |
| **Distinction между `arr[N][M]` и `arr[N][]`** | Эвристика по первым `N*4` байтам |
| **Восстановление `plugin_init` метадаты** | `main.cpp` → `extractPluginInfo` |
| **Три fallback стратегии для plugin info** | `myinfo` → `PLUGIN/VERSION/AUTHOR` глобалы → байткод-скан `register_plugin` |
| **Reducibility check** через T1/T2-редукцию | `block_analysis.cpp` → `IsReducible` |
| **Cooper-Torczon dominator tree** | `block_analysis.cpp` → `ComputeDominators` |

---

## Ограничения

### Что работает
✅ Обычные AMXX плагины с debug-info — **100% функций**  
✅ Плагины без debug-info — декомпилируются, но имена как `_arg0`, `var1`  
✅ Функции с циклами, вложенными if/else, switch, goto  
✅ Строковые массивы, глобалы, статики  
✅ Operator overloading, float операции  

### Что не работает
❌ **Обфусцированные плагины** с ломанными zlib байтами — восстановить невозможно (данные потеряны)  
❌ **SMX (SourcePawn)** — парсинг заголовка есть, но декомпиляция не реализована  
❌ **AMX Mod (не AMXX)** — старый формат, тестировался мало  
❌ **String pool suffix optimization** — иногда выводит суффиксы вместо полных строк (баг оригинального Lysis)  

### Известные quirks
- Некоторые локальные переменные без debug-info могут потерять размерность массива
- Формат `[N][0]` (неявный размер) угадывается эвристически, редко даёт false-positive
- В комментарии `/* ERR load Binary */` попадают выражения в позициях где ожидался чистый lvalue

---

## Дисклеймер

**Этот проект создан исключительно в образовательных и исследовательских целях.**

**Запрещено:**
1. **Красть чужой код** и выдавать за свой
2. **Модифицировать плагины** без разрешения автора
3. **Использовать для взлома** платных / приватных плагинов

**Ты соглашаешься с тем, что:**
1. **Автор не несёт ответственности** за твоё использование инструмента
2. **Вся ответственность** за использование декомпилированного кода — на тебе
3. **Уважай авторские права** оригинальных разработчиков плагинов

**Технические предупреждения:**
1. **Декомпилированный код НЕ ИДЕНТИЧЕН** оригинальному исходнику — теряются комментарии, форматирование, имена (если нет debug-info)
2. **Не все конструкции** восстанавливаются 1:1 — сложная логика может выглядеть иначе, чем в оригинале
3. **Обфусцированные плагины с намеренно битыми zlib байтами** не подлежат восстановлению — это математически невозможно

---


<p align="center">
  <sub>Made with ☕ and C++17</sub>
</p>
