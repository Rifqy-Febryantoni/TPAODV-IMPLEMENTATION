build_lib_example(
  NAME tpaodv-example
  SOURCE_FILES tpaodv-example.cc
  LIBRARIES_TO_LINK
    ${libwifi}
    ${libinternet}
    ${libtpaodv}
    ${libinternet-apps}
)
