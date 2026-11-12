/**
 * @file nec_msh.h
 * @brief NEC-specific diagnostic command extension for the generic infrared MSH.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef NEC_MSH_H
#define NEC_MSH_H

/**
 * @brief Handle `ir codec nec` subcommands using the registered NEC codec.
 * @param argc Number of subcommand arguments (without `ir codec nec`).
 * @param argv Argument vector beginning with read/tx/help.
 */
void infrared_nec_msh(int argc, char **argv);

#endif /* NEC_MSH_H */
