#pragma once

#define ASIO_CB_SHARED(self, method) [this, self] (const asio::error_code &error, size_t len) { method(error, len); }
#define ASIO_CB(method)              [this]       (const asio::error_code &error, size_t len) { method(error, len); }

void initialize_backend(asio::io_service &io);

// EOF
