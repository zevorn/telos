#ifndef TELOS_PLUGINS_MODEL_CATALOG_H
#define TELOS_PLUGINS_MODEL_CATALOG_H

#include <telos/model.h>

bool telos_official_model_catalog_add(struct telos_model_catalog *catalog,
                                      struct telos_error **error);

#endif
