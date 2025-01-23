# Déclare une bibliothèque pour MinioApi
add_library(MinioApi STATIC
    MinioApi.cpp
    MinioApi.h
)

# Spécifie les répertoires d'inclusion pour cette bibliothèque
target_include_directories(MinioApi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Lien avec les modules Qt nécessaires
find_package(Qt6 REQUIRED COMPONENTS Core Network)
target_link_libraries(MinioApi
    PUBLIC
        Qt6::Core
        Qt6::Network
)
