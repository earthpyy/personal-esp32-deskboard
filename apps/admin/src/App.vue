<template>
  <div class="min-h-screen bg-base-200">
    <div class="navbar bg-base-100 shadow-sm">
      <div class="navbar-start">
        <span class="text-xl font-bold px-2">Deskboard</span>
      </div>
      <div class="navbar-center">
        <div role="tablist" class="tabs tabs-box">
          <router-link to="/" class="tab" exact-active-class="tab-active">Calendars</router-link>
          <router-link to="/claude" class="tab" exact-active-class="tab-active">Claude</router-link>
          <router-link to="/settings" class="tab" exact-active-class="tab-active">Settings</router-link>
          <router-link to="/board" class="tab" exact-active-class="tab-active">Status</router-link>
        </div>
      </div>
    </div>

    <div class="max-w-3xl mx-auto p-6 space-y-4">
      <router-view />
    </div>
  </div>
</template>
