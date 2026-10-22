# Build from the repository root:  docker build -f server/Dockerfile -t johto-online-server .
# One image for both services; the command picks which (see docker-compose.yml).
FROM python:3.12-slim
RUN useradd --system --create-home johto && mkdir /data && chown johto /data
WORKDIR /app
COPY tools/party_server.py tools/
COPY server/tokens.py server/relay_service.py server/account_server.py server/
USER johto
ENV PYTHONUNBUFFERED=1 ACCOUNT_DB=/data/accounts.db ACCOUNT_HOST=0.0.0.0
EXPOSE 7373 7374 7380
VOLUME /data
HEALTHCHECK --interval=30s --timeout=3s CMD python -c "import urllib.request as u; u.urlopen(\"http://127.0.0.1:7374/health\", timeout=2)" || exit 1
CMD ["python", "server/relay_service.py"]
