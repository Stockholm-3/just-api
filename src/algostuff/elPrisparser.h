#ifndef ELPRIS_PARSER_H
#define ELPRIS_PARSER_H

// Endast en funktion: tar emot JSON, returnerar array med priser
float* parse_elpriser_fran_json(const char* json_string, int* antal_priser);

#endif
