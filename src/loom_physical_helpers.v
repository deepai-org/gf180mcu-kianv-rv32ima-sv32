// SPDX-License-Identifier: Apache-2.0
// Chip-top-only helper retained outside the chip_core Loom conversion.

`default_nettype none

module async_reset_sync (
    input  wire clk,
    input  wire rst_n_async,
    output wire rst_n_sync
);
  (* async_reg = "true" *) reg [1:0] ff;

  always @(posedge clk or negedge rst_n_async) begin
    if (!rst_n_async) ff <= 2'b00;
    else ff <= {ff[0], 1'b1};
  end

  assign rst_n_sync = ff[1];
endmodule

`default_nettype wire
