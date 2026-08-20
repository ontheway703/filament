# Material语言参考

## 📖 概述

本文档介绍 Filament Material 语言和着色器开发技术。

---

## 1. 基础语法

### 1.1 Material 定义

```glsl
material {
    name : CustomMaterial,
    shadingModel : lit,
    
    parameters : [
        { type : float, name : roughness },
        { type : float3, name : baseColor }
    ]
}
```

### 1.2 着色器代码

```glsl
fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        
        material.baseColor.rgb = materialParams.baseColor;
        material.roughness = materialParams.roughness;
    }
}
```

---

## 2. 高级技术

### 2.1 自定义函数

```glsl
float customLighting(vec3 normal, vec3 lightDir) {
    return max(dot(normal, lightDir), 0.0);
}
```

### 2.2 优化技巧

1. 减少纹理采样
2. 使用内置函数
3. 避免分支

---

## 3. CMakeLists.txt

```cmake
# Compile materials
add_custom_command(
    OUTPUT custom_material.filamat
    COMMAND matc -o custom_material.filamat custom_material.mat
    DEPENDS custom_material.mat
)
```

---

## 4. 相关文档

- [material/04-material-definition.md](../../material/04-material-definition.md)
- [debugging/09-shader-debugging.md](../debugging/09-shader-debugging.md)

---

## 5. 总结

掌握 Material 语言是开发自定义材质的关键。
