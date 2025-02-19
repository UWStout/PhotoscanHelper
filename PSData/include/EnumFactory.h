// These macros serve to make more sophisticated enums
// that support returning their names as strings and
// support a description string.  Note that 'assign'
// may be left empty to use the default value that C
// would give the enum.

// This is based on code by 'Suma' available here:
// https://stackoverflow.com/questions/147267/easy-way-to-use-variables-of-enum-types-as-string-in-c#202511

// expansion macro for enum value definition
#define ENUM_VALUE(name,shortname,descrip) name,

// expansion macro for enum to string conversion
#define ENUM_CASE(name,shortname,descrip) case name: return #name;

// expansion macro for enum to short name conversion
#define ENUM_CASE_SHRT(name,shortname,descrip) case name: return shortname;

// expansion macro for enum to description conversion
#define ENUM_CASE_DESC(name,shortname,descrip) case name: return descrip;

// expansion macro for string to enum conversion
#define ENUM_STRCMP(name,shortname,descrip) if (!strcmp(str,#name)) return name;

/// declare the access function and define enum values
#define DECLARE_ENUM(EnumType,ENUM_DEF) \
  enum EnumType { \
    ENUM_DEF(ENUM_VALUE) \
  }; \
public: \
  static const char* getString(EnumType dummy); \
  static const char* getShortName(EnumType dummy); \
  static const char* getDescription(EnumType dummy); \
  static EnumType get##EnumType##Value(const char *string);

/// define the access function names
#define DEFINE_ENUM(EnumType,ENUM_DEF,ns) \
  const char* ns::getString(ns::EnumType value) \
  { \
    switch(value) \
    { \
      ENUM_DEF(ENUM_CASE) \
    } \
    return ""; /* handle input error */ \
  } \
  const char* ns::getShortName(ns::EnumType value) \
  { \
    switch(value) \
    { \
      ENUM_DEF(ENUM_CASE_SHRT) \
    } \
    return ""; /* handle input error */ \
  } \
  const char* ns::getDescription(ns::EnumType value) \
  { \
    switch(value) \
    { \
      ENUM_DEF(ENUM_CASE_DESC) \
    } \
    return ""; /* handle input error */ \
  } \
  ns::EnumType ns::get##EnumType##Value(const char *str) \
  { \
    ENUM_DEF(ENUM_STRCMP) \
    return static_cast<EnumType>(0); /* handle input error */ \
  }
