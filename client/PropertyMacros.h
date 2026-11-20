#pragma once

#include <QObject>

// --- Базовые строительные блоки ---

#define _PROP_NOTIFY_SIGNAL(name) \
Q_SIGNALS:                        \
    void name##Changed();

#define _PROP_READONLY_BODY(type, ret_type, name) \
public:                                           \
    ret_type name() const { return m_##name; }    \
    _PROP_NOTIFY_SIGNAL(name)                     \
private:                                          \
    type m_##name

#define _PROP_WRITABLE_BODY(type, ret_type, arg_type, name, setter) \
public:                                                             \
    ret_type name() const { return m_##name; }                      \
    void setter(arg_type value)                                     \
    {                                                               \
        if (m_##name != value) {                                    \
            m_##name = value;                                       \
            Q_EMIT name##Changed();                                 \
        }                                                           \
    }                                                               \
    _PROP_NOTIFY_SIGNAL(name)                                       \
private:                                                            \
    type m_##name

// --- Финальные макросы для использования в классах ---

// 1. Read-Only свойство (по значению, для примитивов: int, bool)
#define RO_VAL_PROPERTY(type, name)                      \
    Q_PROPERTY(type name READ name NOTIFY name##Changed) \
    _PROP_READONLY_BODY(type, type, name);

#define RO_VAL_PROPERTY_DEF(type, name, def_val)         \
    Q_PROPERTY(type name READ name NOTIFY name##Changed) \
    _PROP_READONLY_BODY(type, type, name) = def_val;

// 2. Read-Only свойство (по константной ссылке: QString, QVariantList)
#define RO_REF_PROPERTY(type, name)                      \
    Q_PROPERTY(type name READ name NOTIFY name##Changed) \
    _PROP_READONLY_BODY(type, const type&, name);

#define RO_REF_PROPERTY_DEF(type, name, def_val)         \
    Q_PROPERTY(type name READ name NOTIFY name##Changed) \
    _PROP_READONLY_BODY(type, const type&, name) = def_val;

// 3. Read-Write свойство (по значению)
#define RW_VAL_PROPERTY(type, name, setter)                           \
    Q_PROPERTY(type name READ name WRITE setter NOTIFY name##Changed) \
    _PROP_WRITABLE_BODY(type, type, type, name, setter);

#define RW_VAL_PROPERTY_DEF(type, name, setter, def_val)              \
    Q_PROPERTY(type name READ name WRITE setter NOTIFY name##Changed) \
    _PROP_WRITABLE_BODY(type, type, type, name, setter) = def_val;

// 4. Read-Write свойство (по константной ссылке)
#define RW_REF_PROPERTY(type, name, setter)                           \
    Q_PROPERTY(type name READ name WRITE setter NOTIFY name##Changed) \
    _PROP_WRITABLE_BODY(type, const type&, const type&, name, setter);

#define RW_REF_PROPERTY_DEF(type, name, setter, def_val)              \
    Q_PROPERTY(type name READ name WRITE setter NOTIFY name##Changed) \
    _PROP_WRITABLE_BODY(type, const type&, const type&, name, setter) = def_val;
