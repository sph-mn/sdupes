(pre-include-guard-begin sph-status-h)

(sc-comment
  "return status as integer code with group identifier"
  "for exception handling with a local variable and a goto label"
  "status id 0 is success, everything else can be considered a special case or failure"
  "status ids are signed integers for compatibility with error return codes from other existing libraries"
  "group ids are strings, used to categorise sets of errors codes from different libraries for example")

(pre-include "inttypes.h")
(declare status-t (type (struct (id int) (group uint8-t*))))

(pre-define
  status-id-success 0
  status-group-undefined (convert-type "" uint8-t*)
  status-declare (define status status-t (struct-literal status-id-success status-group-undefined))
  status-is-success (= status-id-success status.id)
  status-is-failure (not status-is-success)
  status-return (return status)
  status-i-return (return status.id)
  status-goto (goto exit)
  (status-set group-id status-id)
  (set status.group (convert-type group-id uint8-t*) status.id status-id)
  (status-set-goto group-id status-id) (begin (status-set group-id status-id) status-goto)
  (status-require expression) (begin (set status expression) (if status-is-failure status-goto))
  (status-i-require expression) (begin (set status.id expression) (if status-is-failure status-goto))
  (status-require-return expression)
  (begin (set status expression) (if status-is-failure status-return)))

(pre-include-guard-end)