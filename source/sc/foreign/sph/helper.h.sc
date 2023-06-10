(pre-include-guard-begin sph-helper-h)
(sc-comment "depends on status.h")
(pre-include "inttypes.h")
(enum (sph-helper-status-id-memory))

(pre-define
  sph-helper-status-group (convert-type "sph" uint8-t*)
  (sph-helper-malloc size result)
  (begin
    "add explicit type cast to prevent compiler warning"
    (sph-helper-primitive-malloc size (convert-type result void**)))
  (sph-helper-malloc-string size result)
  (sph-helper-primitive-malloc-string size (convert-type result uint8-t**))
  (sph-helper-calloc size result) (sph-helper-primitive-calloc size (convert-type result void**))
  (sph-helper-realloc size result) (sph-helper-primitive-realloc size (convert-type result void**)))

(declare
  (sph-helper-status-description a) (uint8-t* status-t)
  (sph-helper-status-name a) (uint8-t* status-t)
  (sph-helper-primitive-malloc size result) (status-t size-t void**)
  (sph-helper-primitive-malloc-string length result) (status-t size-t uint8-t**)
  (sph-helper-primitive-calloc size result) (status-t size-t void**)
  (sph-helper-primitive-realloc size memory) (status-t size-t void**)
  (sph-helper-display-bits-u8 a) (void uint8-t)
  (sph-helper-display-bits a size) (void void* size-t))

(pre-include-guard-end)