# The configuration check already read and validated the same files.
set(recheck OFF)
foreach(input IN LISTS INPUTS)
    if("${input}" IS_NEWER_THAN "${CONFIGURED}")
        set(recheck ON)
        break()
    endif()
endforeach()

if(recheck)
    execute_process(
        COMMAND "${PYTHON}" -I -X utf8 "${ROOT}/scripts/check_lift_hooks.py" "${LIFT}"
                --profile "${PROFILE}" --stamp "${STAMP}"
        RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Lift hook verification failed")
    endif()
else()
    file(TOUCH "${STAMP}")
endif()
