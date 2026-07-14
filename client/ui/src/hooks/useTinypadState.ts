import { useEffect, useState } from "react";
import { subscribeState } from "@/lib/native";
import { EMPTY_STATE, type TinypadState } from "@/types";

export function useTinypadState(): TinypadState {
  const [state, setState] = useState<TinypadState>(EMPTY_STATE);
  useEffect(() => subscribeState(setState), []);
  return state;
}
