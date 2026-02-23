if(NOT DEFINED URL OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "DownloadFile.cmake requires -DURL and -DOUTPUT.")
endif()

if(EXISTS "${OUTPUT}")
  message(STATUS "Using cached download: ${OUTPUT}")
  return()
endif()

get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

message(STATUS "Downloading ${URL}")
file(
  DOWNLOAD
  "${URL}"
  "${OUTPUT}"
  SHOW_PROGRESS
  TLS_VERIFY ON
  STATUS DOWNLOAD_STATUS
)

list(GET DOWNLOAD_STATUS 0 DOWNLOAD_CODE)
list(GET DOWNLOAD_STATUS 1 DOWNLOAD_MESSAGE)

if(NOT DOWNLOAD_CODE EQUAL 0)
  file(REMOVE "${OUTPUT}")
  message(
    FATAL_ERROR
      "Failed to download '${URL}' (code=${DOWNLOAD_CODE} message='${DOWNLOAD_MESSAGE}')"
  )
endif()
