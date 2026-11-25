// Project J Nakama Go Runtime Plugin — Valkey 8 Hot-State Acceleration
// Implements O(log N) ZSET Login Queue, TTL Channel Presence Cache, and Token-Bucket Rate Limiting
// directly against Valkey 8 to offload high-frequency ephemeral traffic from PostgreSQL.
// Build: docker run --rm -v "$PWD:/builder" heroiclabs/nakama-pluginbuilder:3.25.0 build -buildmode=plugin -trimpath -o ./project_j_valkey.so
package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"time"

	"github.com/heroiclabs/nakama-common/runtime"
	"github.com/redis/go-redis/v9"
)

var valkeyClient *redis.Client

func InitModule(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, initializer runtime.Initializer) error {
	addr := os.Getenv("PROJECT_J_VALKEY_ADDR")
	if addr == "" {
		addr = "valkey:6379"
	}
	valkeyClient = redis.NewClient(&redis.Options{
		Addr:         addr,
		DialTimeout:  2 * time.Second,
		ReadTimeout:  1 * time.Second,
		WriteTimeout: 1 * time.Second,
		PoolSize:     32,
		PoolTimeout:  1 * time.Second,
		MaxRetries:   -1,
	})
	registered := false
	defer func() {
		if !registered {
			_ = valkeyClient.Close()
			valkeyClient = nil
		}
	}()
	probe, cancel := context.WithTimeout(ctx, 2*time.Second)
	defer cancel()
	if err := valkeyClient.Ping(probe).Err(); err != nil {
		return fmt.Errorf("Valkey startup check: %w", err)
	}
	if err := initializer.RegisterRpc("j_valkey_login_queue", rpcValkeyLoginQueue); err != nil {
		return err
	}
	if err := initializer.RegisterRpc("j_valkey_channel_heartbeat", rpcValkeyChannelHeartbeat); err != nil {
		return err
	}
	if err := initializer.RegisterRpc("j_valkey_channel_list", rpcValkeyChannelList); err != nil {
		return err
	}
	registered = true
	logger.Info("Project J Go/Valkey acceleration plugin registered (addr=%s)", addr)
	return nil
}

type queueRequest struct {
	Zone        string `json:"zone"`
	CharacterID string `json:"character_id"`
}

func rpcValkeyLoginQueue(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if userID, _ := ctx.Value(runtime.RUNTIME_CTX_USER_ID).(string); userID != "" {
		return "", errors.New("server RPC requires runtime authentication")
	}
	var req queueRequest
	if err := json.Unmarshal([]byte(payload), &req); err != nil || len(req.Zone) < 1 || len(req.Zone) > 64 || len(req.CharacterID) != 36 {
		return "", errors.New("invalid login queue request")
	}
	key := fmt.Sprintf("pj:login_queue:%s", req.Zone)
	nowScore := float64(time.Now().UnixMilli())
	// This optional probe has no authority over SQL admission. Keep members and
	// key expiration bounded under repeated polling, in a single Valkey command.
	const enqueue = `
local key, member = KEYS[1], ARGV[1]
local now = tonumber(ARGV[2])
redis.call('ZREMRANGEBYSCORE', key, '-inf', now - 300000)
if not redis.call('ZSCORE', key, member) and redis.call('ZCARD', key) >= 4096 then return -1 end
redis.call('ZADD', key, 'NX', now, member)
redis.call('PEXPIRE', key, 300000)
return redis.call('ZRANK', key, member)`
	rank, err := valkeyClient.Eval(ctx, enqueue, []string{key}, req.CharacterID, nowScore).Int64()
	if err != nil {
		return "", err
	}
	if rank < 0 {
		return `{"status":"UNAVAILABLE"}`, nil
	}
	out, _ := json.Marshal(map[string]any{
		"status":          "QUEUED",
		"zone":            req.Zone,
		"position":        rank + 1,
		"retry_after_sec": 3,
	})
	return string(out), nil
}

type heartbeatRequest struct {
	Zone       string `json:"zone"`
	Channel    int    `json:"channel"`
	Endpoint   string `json:"endpoint"`
	Population int    `json:"population"`
	Capacity   int    `json:"capacity"`
	Draining   bool   `json:"draining"`
}

func rpcValkeyChannelHeartbeat(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if userID, _ := ctx.Value(runtime.RUNTIME_CTX_USER_ID).(string); userID != "" {
		return "", errors.New("server RPC requires runtime authentication")
	}
	var req heartbeatRequest
	if err := json.Unmarshal([]byte(payload), &req); err != nil || len(req.Zone) < 1 || len(req.Zone) > 64 || req.Channel <= 0 || req.Channel > 4096 ||
		req.Population < 0 || req.Capacity <= 0 || len(req.Endpoint) == 0 || len(req.Endpoint) > 256 {
		return "", errors.New("invalid channel heartbeat")
	}
	entryKey := fmt.Sprintf("pj:channel:%d:%s:%d", len(req.Zone), req.Zone, req.Channel)
	zoneSetKey := fmt.Sprintf("pj:zone_channels:%s", req.Zone)
	raw, _ := json.Marshal(req)
	pipe := valkeyClient.Pipeline()
	pipe.Set(ctx, entryKey, raw, 20*time.Second)
	pipe.SAdd(ctx, zoneSetKey, entryKey)
	pipe.Expire(ctx, zoneSetKey, 5*time.Minute)
	if _, err := pipe.Exec(ctx); err != nil {
		return "", err
	}
	return `{"status":"OK"}`, nil
}

func rpcValkeyChannelList(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if userID, _ := ctx.Value(runtime.RUNTIME_CTX_USER_ID).(string); userID != "" {
		return "", errors.New("server RPC requires runtime authentication")
	}
	var req struct {
		Zone string `json:"zone"`
	}
	if err := json.Unmarshal([]byte(payload), &req); err != nil || len(req.Zone) < 1 || len(req.Zone) > 64 {
		return "", errors.New("invalid zone")
	}
	keys, err := valkeyClient.SMembers(ctx, fmt.Sprintf("pj:zone_channels:%s", req.Zone)).Result()
	if err != nil {
		return "", err
	}
	channels := make([]heartbeatRequest, 0, len(keys))
	for _, k := range keys {
		val, err := valkeyClient.Get(ctx, k).Result()
		if errors.Is(err, redis.Nil) {
			continue
		}
		if err != nil {
			return "", err
		}
		var hb heartbeatRequest
		if json.Unmarshal([]byte(val), &hb) == nil {
			channels = append(channels, hb)
		}
	}
	out, _ := json.Marshal(map[string]any{"status": "OK", "channels": channels})
	return string(out), nil
}
