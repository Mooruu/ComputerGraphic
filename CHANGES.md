# Список изменений в Lab4

## Краткое описание доработок

В лабораторную работу добавлены следующие возможности:
1. ✅ Загрузка текстур (WIC + DDS форматы)
2. ✅ Отрисовка OBJ моделей с расширенной поддержкой материалов
3. ✅ Текстурная анимация и тайлинг с управлением в реальном времени

---

## Детальный список изменений

### 1. main.cpp (863 строки)

#### Добавленные include:
```cpp
#include "Common/DDSTextureLoader.h"
```

#### Новые структуры:
```cpp
struct MaterialRootConstants
{
    XMFLOAT4 DiffuseAlbedo;
    XMFLOAT4 SpecularAlbedo;      // rgb = цвет, a = shininess
    XMFLOAT4 AmbientColor;
    XMFLOAT4 EmissiveColor;
    XMFLOAT4 UVTilingOffset;      // xy = tiling, zw = UV offset
    XMFLOAT4 Flags;               // x,y,z = флаги текстур
};
```

#### Новые функции:
- `LoadDDSTextureFromFile()` - загрузка DDS текстур с мипмапами
- `LoadTextureFromFile()` - универсальный загрузчик (автовыбор WIC/DDS)
- `ConfigureMaterialAnimations()` - настройка анимации по типу материала
- `OnKeyboardInput()` - интерактивное управление анимацией

#### Изменённые функции:
- `BuildTextures()` - использует LoadTextureFromFile, поддержка мипмапов
- `Draw()` - передача расширенных параметров материалов в шейдер
- `Update()` - добавлен вызов OnKeyboardInput()
- `BuildModel()` - вызов ConfigureMaterialAnimations()

#### Удалённые переменные:
- `mTextureTile` - заменено на индивидуальные настройки материалов
- `mTextureScrollSpeed` - заменено на индивидуальные настройки

### 2. Model.h (65 строк)

#### Расширенная структура ModelMaterial:
```cpp
struct ModelMaterial
{
    // Базовые свойства
    std::string Name;
    XMFLOAT4 DiffuseAlbedo;
    XMFLOAT3 SpecularAlbedo;
    float Shininess;
    XMFLOAT3 AmbientColor;
    XMFLOAT3 EmissiveColor;
    
    // Текстуры
    std::wstring DiffuseTexture;
    std::wstring SpecularTexture;
    std::wstring NormalTexture;
    std::wstring BumpTexture;
    std::wstring AmbientTexture;
    
    // Флаги
    bool HasTexture;
    bool HasSpecularTexture;
    bool HasNormalTexture;
    bool HasBumpTexture;
    bool HasAmbientTexture;
    
    // Анимация
    XMFLOAT2 TextureTiling;
    XMFLOAT2 TextureScrollSpeed;
};
```

### 3. Model.cpp (255 строк)

#### Изменения в LoadFromOBJ():
- Загрузка specular параметров (`src.specular`, `src.shininess`)
- Загрузка ambient цвета (`src.ambient`)
- Загрузка emissive цвета (`src.emission`)
- Загрузка всех типов текстур:
  - `src.specular_texname` → SpecularTexture
  - `src.normal_texname` → NormalTexture
  - `src.bump_texname` → BumpTexture
  - `src.ambient_texname` → AmbientTexture

### 4. sponza.hlsl (102 строки)

#### Обновлённый cbuffer:
```hlsl
cbuffer cbPerMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float4 gSpecularAlbedo;
    float4 gAmbientColor;
    float4 gEmissiveColor;
    float4 gUVTilingOffset;
    float4 gMaterialFlags;
};
```

#### Улучшенный пиксельный шейдер:
- Модель освещения Blinn-Phong
- Расчёт specular highlights с использованием half-vector
- Поддержка ambient освещения (30%)
- Поддержка emissive свечения
- Gamma correction 2.2 (было 2.0)
- Правильное затухание specular при NdotL = 0

---

## Управление приложением

### Камера:
- **ЛКМ + движение мыши** - вращение камеры
- **ПКМ + движение мыши** - zoom

### Текстурная анимация (клавиши 1-6, 0):
| Клавиша | Действие |
|---------|----------|
| **1** | Увеличить тайлинг (больше повторений) |
| **2** | Уменьшить тайлинг (меньше повторений) |
| **3** | Ускорить прокрутку по оси X → |
| **4** | Замедлить прокрутку по оси X ← |
| **5** | Ускорить прокрутку по оси Y ↑ |
| **6** | Замедлить прокрутку по оси Y ↓ |
| **0** | Сбросить все настройки |

---

## Автоматические настройки материалов

При загрузке модели, материалы автоматически настраиваются по имени:

| Тип материала | Тайлинг | Скорость анимации |
|---------------|---------|-------------------|
| **water** (вода) | 4.0 × 4.0 | (0.05, 0.02) |
| **fabric/curtain** (ткань) | 3.0 × 3.0 | (0.0, 0.01) |
| **floor** (пол) | 8.0 × 8.0 | (0.0, 0.0) |
| **wall** (стена) | 2.0 × 2.0 | (0.0, 0.0) |
| **default** (остальное) | 1.0 × 1.0 | (0.0, 0.0) |

---

## Технические улучшения

### Загрузка текстур:
- WIC поддерживает: PNG, JPG, BMP, TIFF, GIF
- DDS поддерживает: сжатые форматы (BC1-BC7), мипмапы, cube maps
- Автоматический fallback на белую текстуру при ошибке
- Поддержка всех мипмапов в SRV (`MipLevels = -1`)

### Освещение:
- 2 источника света (тёплый + холодный)
- Ambient компонент: 30% от base color
- Specular highlights: Blinn-Phong с shininess из материала
- Emissive: аддитивное свечение
- Gamma correction: 1/2.2

### Производительность:
- Root constants для материалов (быстрее чем CBV)
- Descriptor table для текстур (один set на draw call)
- Индексированная геометрия с дедупликацией вершин

---

## Проверка перед сборкой

Убедитесь что в проекте есть:
- ✅ `Common/DDSTextureLoader.h` и `.cpp`
- ✅ `tiny_obj_loader.h`
- ✅ Все файлы из папки `Common/`
- ✅ Модель в `Models/sponza.obj` (опционально)

---

## Известные ограничения

1. Normal/Bump карты загружаются, но не применяются в шейдере (требует tangent space)
2. Specular текстуры загружаются, но используется только specular color из MTL
3. Анимация применяется ко всем материалам одновременно при нажатии клавиш

---

## Возможные доработки

- [ ] Реализация normal mapping (требует tangent/bitangent)
- [ ] Использование specular текстур для gloss mapping
- [ ] Индивидуальное управление анимацией материалов через UI
- [ ] PBR материалы (metallic-roughness workflow)
- [ ] IBL (Image-Based Lighting)
- [ ] Shadow mapping

---

**Дата изменений:** 13 июня 2026  
**Статус:** Все задачи выполнены ✅
