/**
 * cli_errors.h - Error codes for the cdo_cli framework.
 *
 * All public cdo_cli functions return 0 on success, non-zero on error.
 */
#ifndef CDO_CLI_ERRORS_H
#define CDO_CLI_ERRORS_H

#define CLI_OK              0   /* Success */
#define CLI_ERR_PARSE       1   /* Argument parsing error (unknown option, missing value) */
#define CLI_ERR_VALIDATE    2   /* Validation failure (type mismatch, range, required missing) */
#define CLI_ERR_DUPLICATE   3   /* Duplicate command registration */
#define CLI_ERR_NOT_FOUND   4   /* Command not found in registry */
#define CLI_ERR_BUFFER      5   /* Caller-provided buffer too small */
#define CLI_ERR_ALLOC       6   /* Memory allocation failure */
#define CLI_ERR_PLATFORM    7   /* Platform API call failed */

#endif /* CDO_CLI_ERRORS_H */
