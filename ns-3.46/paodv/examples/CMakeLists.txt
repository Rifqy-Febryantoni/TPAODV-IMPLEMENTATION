build_lib_example(
  NAME paodv-example
  SOURCE_FILES paodv-example.cc
  LIBRARIES_TO_LINK
    ${libwifi}
    ${libinternet}
    ${libpaodv}
    ${libinternet-apps}
)
